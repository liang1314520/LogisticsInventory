#include "logistics_dialog.h"

#include "services/inventory_query_service.h"
#include "services/logistics_service.h"
#include "services/master_data_service.h"

#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

LogisticsDialog::LogisticsDialog(MasterDataService &masterData, InventoryQueryService &queries,
                                 LogisticsService &service, DocumentType type, QWidget *parent)
    : QDialog(parent), masterData_(masterData), queries_(queries), service_(service), type_(type)
{
    setObjectName(QStringLiteral("logisticsDialog"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    documentNo_ = new QLineEdit(this);
    documentNo_->setObjectName(QStringLiteral("documentNo"));
    source_ = new QComboBox(this);
    source_->setObjectName(QStringLiteral("sourceLocationId"));
    destination_ = new QComboBox(this);
    destination_->setObjectName(QStringLiteral("destinationLocationId"));
    businessAt_ = new QDateTimeEdit(QDateTime::currentDateTime(), this);
    businessAt_->setObjectName(QStringLiteral("businessAt"));
    businessAt_->setCalendarPopup(true);
    note_ = new QTextEdit(this);
    note_->setObjectName(QStringLiteral("note"));
    form->addRow(QStringLiteral("单号"), documentNo_);
    form->addRow(QStringLiteral("来源"), source_);
    form->addRow(QStringLiteral("目的地"), destination_);
    form->addRow(QStringLiteral("业务时间"), businessAt_);
    form->addRow(QStringLiteral("备注"), note_);
    layout->addLayout(form);

    lines_ = new QTableWidget(0, 2, this);
    lines_->setObjectName(QStringLiteral("lines"));
    lines_->setHorizontalHeaderLabels({QStringLiteral("批次"), QStringLiteral("数量")});
    lines_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(lines_);

    auto *lineActions = new QHBoxLayout;
    add_ = new QPushButton(QStringLiteral("添加明细"), this);
    add_->setObjectName(QStringLiteral("addLineButton"));
    remove_ = new QPushButton(QStringLiteral("删除明细"), this);
    remove_->setObjectName(QStringLiteral("removeLineButton"));
    lineActions->addWidget(add_);
    lineActions->addWidget(remove_);
    lineActions->addStretch();
    layout->addLayout(lineActions);

    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("errorLabel"));
    error_->setStyleSheet(QStringLiteral("color:#b00020"));
    error_->setWordWrap(true);
    layout->addWidget(error_);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    save_ = buttons->button(QDialogButtonBox::Save);
    save_->setObjectName(QStringLiteral("saveButton"));
    layout->addWidget(buttons);

    connect(source_, &QComboBox::currentIndexChanged, this,
            [this] { setSourceLocation(source_->currentData().toLongLong()); });
    connect(add_, &QPushButton::clicked, this, [this] {
        const auto result = addLine();
        if (!result.ok())
            showError(result.error());
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        if (lines_->currentRow() >= 0)
            lines_->removeRow(lines_->currentRow());
    });
    connect(save_, &QPushButton::clicked, this, [this] {
        if (save())
            accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

Result<bool> LogisticsDialog::loadLocations(EntityId currentSource, EntityId currentDestination)
{
    const auto result = masterData_.enabledLocationsResult();
    if (!result.ok()) {
        showError(result.error());
        return Result<bool>::failure(result.error());
    }

    source_->blockSignals(true);
    source_->clear();
    for (const auto &location : result.value()) {
        if (location.countsAsInventory)
            source_->addItem(location.name, location.id);
    }
    if (currentSource > 0 && source_->findData(currentSource) < 0) {
        const auto historical = masterData_.locationById(currentSource);
        if (!historical.ok()) {
            source_->blockSignals(false);
            showError(historical.error());
            return Result<bool>::failure(historical.error());
        }
        source_->addItem(historical.value().name + QStringLiteral(" (已停用/历史)"),
                         historical.value().id);
    }
    const int sourceIndex = source_->findData(currentSource);
    if (sourceIndex >= 0)
        source_->setCurrentIndex(sourceIndex);
    source_->blockSignals(false);
    return refreshDestinations(currentDestination, currentDestination > 0);
}

Result<bool> LogisticsDialog::refreshDestinations(EntityId currentDestination,
                                                   bool preserveHistorical)
{
    const EntityId selected = currentDestination > 0 ? currentDestination
                                                       : destination_->currentData().toLongLong();
    destination_->clear();
    const auto result = masterData_.enabledLocationsResult();
    if (!result.ok()) {
        showError(result.error());
        return Result<bool>::failure(result.error());
    }

    const EntityId sourceId = source_->currentData().toLongLong();
    for (const auto &location : result.value()) {
        const bool valid = type_ == DocumentType::Transfer
            ? location.countsAsInventory && location.id != sourceId
            : !location.countsAsInventory;
        if (valid)
            destination_->addItem(location.name, location.id);
    }
    if (preserveHistorical && selected > 0 && destination_->findData(selected) < 0) {
        const auto historical = masterData_.locationById(selected);
        if (!historical.ok()) {
            showError(historical.error());
            return Result<bool>::failure(historical.error());
        }
        destination_->addItem(historical.value().name + QStringLiteral(" (已停用/历史)"),
                               historical.value().id);
    }
    const int index = destination_->findData(selected);
    if (index >= 0)
        destination_->setCurrentIndex(index);
    return Result<bool>::success(true);
}

Result<bool> LogisticsDialog::initialize(EntityId id, bool readOnly)
{
    id_ = id;
    readOnly_ = readOnly;
    if (id) {
        const auto loaded = service_.byId(id);
        if (!loaded.ok()) {
            showError(loaded.error());
            return Result<bool>::failure(loaded.error());
        }
        if (loaded.value().type != type_)
            return Result<bool>::failure({QStringLiteral("invalid_type"),
                                          QStringLiteral("单据类型不匹配"),
                                          QStringLiteral("type")});
        if (!readOnly && loaded.value().status != DocumentStatus::Draft)
            return Result<bool>::failure({QStringLiteral("invalid_status"),
                                          QStringLiteral("仅草稿可编辑"),
                                          QStringLiteral("status")});

        const auto locations = loadLocations(loaded.value().sourceLocationId,
                                             loaded.value().destinationLocationId);
        if (!locations.ok())
            return locations;
        const auto batchResult = queries_.availableBatches(loaded.value().sourceLocationId);
        if (!batchResult.ok()) {
            showError(batchResult.error());
            return Result<bool>::failure(batchResult.error());
        }
        batches_.clear();
        for (const auto &batch : batchResult.value()) {
            batches_.append({batch.batchId, batch.productTypeId,
                             QStringLiteral("%1 / %2 / %3")
                                 .arg(batch.batchNo, batch.typeCode,
                                      QString::number(batch.quantity))});
        }
        documentNo_->setText(loaded.value().documentNo);
        businessAt_->setDateTime(loaded.value().businessAt);
        note_->setPlainText(loaded.value().note);
        const int destinationIndex =
            destination_->findData(loaded.value().destinationLocationId);
        if (destinationIndex >= 0)
            destination_->setCurrentIndex(destinationIndex);
        lines_->setRowCount(0);
        for (const auto &line : loaded.value().lines) {
            const auto populated = populateLine(lines_->rowCount(), line, &line);
            if (!populated.ok())
                return populated;
        }
    } else {
        const auto locations = loadLocations();
        if (!locations.ok())
            return locations;
        if (source_->count()) {
            const auto sourceResult = setSourceLocation(source_->currentData().toLongLong());
            if (!sourceResult.ok())
                return sourceResult;
        }
    }

    const QList<QWidget *> controls{documentNo_, source_, destination_, businessAt_, note_,
                                    lines_, add_, remove_};
    for (QWidget *widget : controls)
        widget->setEnabled(!readOnly_);
    save_->setVisible(!readOnly_);
    return Result<bool>::success(true);
}

Result<bool> LogisticsDialog::setSourceLocation(EntityId id)
{
    const int sourceIndex = source_->findData(id);
    if (sourceIndex < 0) {
        const ServiceError value{QStringLiteral("not_found"), QStringLiteral("来源地点不存在"),
                                 QStringLiteral("sourceLocationId")};
        showError(value);
        return Result<bool>::failure(value);
    }
    if (source_->currentIndex() != sourceIndex) {
        source_->blockSignals(true);
        source_->setCurrentIndex(sourceIndex);
        source_->blockSignals(false);
    }
    for (int index = source_->count() - 1; index >= 0; --index) {
        if (source_->itemText(index).contains(QStringLiteral("已停用/历史")) &&
            source_->itemData(index).toLongLong() != id)
            source_->removeItem(index);
    }

    const auto destinations = refreshDestinations();
    if (!destinations.ok())
        return destinations;
    const auto result = queries_.availableBatches(id);
    if (!result.ok()) {
        batches_.clear();
        for (int row = 0; row < lines_->rowCount(); ++row) {
            if (auto *batch = qobject_cast<QComboBox *>(lines_->cellWidget(row, 0)))
                batch->clear();
        }
        showError(result.error());
        return Result<bool>::failure(result.error());
    }

    batches_.clear();
    for (const auto &batch : result.value()) {
        batches_.append({batch.batchId, batch.productTypeId,
                         QStringLiteral("%1 / %2 / %3")
                             .arg(batch.batchNo, batch.typeCode,
                                  QString::number(batch.quantity))});
    }
    for (int row = 0; row < lines_->rowCount(); ++row) {
        auto *quantity = qobject_cast<QLineEdit *>(lines_->cellWidget(row, 1));
        bool ok = false;
        const qint64 amount = quantity ? quantity->text().toLongLong(&ok) : 0;
        const LogisticsLineDraft line{0, ok ? amount : 0};
        const auto populated = populateLine(row, line);
        if (!populated.ok())
            return populated;
    }
    return Result<bool>::success(true);
}

QVector<EntityId> LogisticsDialog::visibleBatchIds() const
{
    QVector<EntityId> ids;
    for (const auto &batch : batches_)
        ids.append(batch.id);
    return ids;
}

Result<bool> LogisticsDialog::addLine()
{
    return populateLine(lines_->rowCount(), {});
}

Result<bool> LogisticsDialog::populateLine(int row, const LogisticsLineDraft &line,
                                            const LogisticsLine *persistedLine)
{
    if (row >= lines_->rowCount())
        lines_->insertRow(row);

    auto *batch = qobject_cast<QComboBox *>(lines_->cellWidget(row, 0));
    if (!batch)
        batch = new QComboBox(lines_);
    lines_->setCellWidget(row, 0, batch);
    batch->blockSignals(true);
    batch->clear();
    for (const auto &choice : batches_)
        batch->addItem(choice.text, choice.id);
    if (line.batchId > 0 && batch->findData(line.batchId) < 0) {
        const QString text = persistedLine
            ? QStringLiteral("%1 / %2 (历史批次)")
                  .arg(persistedLine->batchNoSnapshot, persistedLine->typeCodeSnapshot)
            : QStringLiteral("批次 %1 (历史批次)").arg(line.batchId);
        batch->addItem(text, line.batchId);
    }
    const int batchIndex = batch->findData(line.batchId);
    if (batchIndex >= 0)
        batch->setCurrentIndex(batchIndex);
    batch->blockSignals(false);

    auto *quantity = qobject_cast<QLineEdit *>(lines_->cellWidget(row, 1));
    if (!quantity) {
        quantity = new QLineEdit(lines_);
        quantity->setValidator(new QRegularExpressionValidator(
            QRegularExpression(QStringLiteral("[1-9][0-9]{0,18}")), quantity));
        lines_->setCellWidget(row, 1, quantity);
    }
    quantity->setText(line.quantity > 0 ? QString::number(line.quantity)
                                        : (line.batchId > 0 ? QStringLiteral("0")
                                                            : QStringLiteral("1")));
    return Result<bool>::success(true);
}

Result<bool> LogisticsDialog::setLineBatch(int row, EntityId batchId)
{
    auto *combo = qobject_cast<QComboBox *>(lines_->cellWidget(row, 0));
    if (!combo)
        return Result<bool>::failure({QStringLiteral("not_found"), QStringLiteral("明细行不存在"),
                                      QStringLiteral("lines[%1].batchId").arg(row)});
    const int index = combo->findData(batchId);
    if (index < 0)
        return Result<bool>::failure({QStringLiteral("not_found"), QStringLiteral("批次不可用"),
                                      QStringLiteral("lines[%1].batchId").arg(row)});
    if (combo->currentIndex() != index)
        combo->setCurrentIndex(index);
    return Result<bool>::success(true);
}

Result<bool> LogisticsDialog::setDraft(const LogisticsDraft &value)
{
    documentNo_->setText(value.documentNo);
    businessAt_->setDateTime(value.businessAt.isValid() ? value.businessAt
                                                         : QDateTime::currentDateTime());
    note_->setPlainText(value.note);
    const int sourceIndex = source_->findData(value.sourceLocationId);
    if (sourceIndex >= 0)
        source_->setCurrentIndex(sourceIndex);
    const auto sourceResult = setSourceLocation(value.sourceLocationId);
    if (!sourceResult.ok())
        return sourceResult;
    const int destinationIndex = destination_->findData(value.destinationLocationId);
    if (destinationIndex >= 0)
        destination_->setCurrentIndex(destinationIndex);
    lines_->setRowCount(0);
    for (const auto &line : value.lines) {
        const auto populated = populateLine(lines_->rowCount(), line);
        if (!populated.ok())
            return populated;
    }
    return Result<bool>::success(true);
}

LogisticsDraft LogisticsDialog::draft() const
{
    LogisticsDraft value{documentNo_->text(), type_, source_->currentData().toLongLong(),
                         destination_->currentData().toLongLong(), businessAt_->dateTime(),
                         note_->toPlainText(), {}};
    for (int row = 0; row < lines_->rowCount(); ++row) {
        auto *batch = qobject_cast<QComboBox *>(lines_->cellWidget(row, 0));
        auto *quantity = qobject_cast<QLineEdit *>(lines_->cellWidget(row, 1));
        bool ok = false;
        const qint64 amount = quantity ? quantity->text().toLongLong(&ok) : 0;
        value.lines.append({batch ? batch->currentData().toLongLong() : 0,
                            ok ? amount : 0});
    }
    return value;
}

bool LogisticsDialog::save()
{
    const auto result = id_
        ? service_.updateDraft(id_, draft())
        : [&] {
              const auto created = service_.createDraft(draft());
              return created.ok() ? Result<bool>::success(true)
                                  : Result<bool>::failure(created.error());
          }();
    if (!result.ok()) {
        showError(result.error());
        return false;
    }
    error_->clear();
    return true;
}

QWidget *LogisticsDialog::controlForField(const QString &field) const
{
    if (field == QStringLiteral("documentNo"))
        return documentNo_;
    if (field == QStringLiteral("sourceLocationId"))
        return source_;
    if (field == QStringLiteral("destinationLocationId"))
        return destination_;
    if (field == QStringLiteral("businessAt"))
        return businessAt_;
    const QRegularExpression lineField(QStringLiteral("^lines\\[(\\d+)\\]\\.(batchId|quantity)$"));
    const auto match = lineField.match(field);
    if (match.hasMatch()) {
        const int row = match.captured(1).toInt();
        const int column = match.captured(2) == QStringLiteral("batchId") ? 0 : 1;
        if (row >= 0 && row < lines_->rowCount())
            return lines_->cellWidget(row, column);
    }
    if (field == QStringLiteral("lines"))
        return lines_;
    return nullptr;
}

void LogisticsDialog::showError(const ServiceError &value)
{
    error_->setText(value.message);
    if (auto *control = controlForField(value.field))
        control->setFocus();
}

bool LogisticsDialog::isReadOnly() const
{
    return readOnly_;
}
