#include "logistics_dialog.h"

#include "services/inventory_query_service.h"
#include "services/logistics_service.h"
#include "services/master_data_service.h"
#include "ui/widgets/date_time_input.h"
#include "ui/widgets/layout_utils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>

LogisticsDialog::LogisticsDialog(MasterDataService &masterData, InventoryQueryService &queries,
                                 LogisticsService &service, DocumentType type, QWidget *parent)
    : QDialog(parent), masterData_(masterData), queries_(queries), service_(service), type_(type)
{
    setObjectName(QStringLiteral("logisticsDialog"));
    setModal(true);
    setSizeGripEnabled(true);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    documentNo_ = new QLineEdit(this);
    documentNo_->setObjectName(QStringLiteral("documentNo"));
    documentNo_->setReadOnly(true);
    documentNo_->setPlaceholderText(QStringLiteral("保存时自动生成"));
    manualNumber_ = new QCheckBox(QStringLiteral("手动编号"), this);
    manualNumber_->setObjectName(QStringLiteral("manualNumber"));
    auto *numberLayout = new QHBoxLayout;
    numberLayout->addWidget(documentNo_, 1);
    numberLayout->addWidget(manualNumber_);
    connect(manualNumber_, &QCheckBox::toggled, this, [this](bool manual) {
        documentNo_->setReadOnly(!manual);
        if (!manual)
            documentNo_->setText(originalNumber_);
    });
    source_ = new QComboBox(this);
    source_->setObjectName(QStringLiteral("sourceLocationId"));
    destination_ = new QComboBox(this);
    destination_->setObjectName(QStringLiteral("destinationLocationId"));
    businessAt_ = new DateTimeInput(this);
    businessAt_->setObjectName(QStringLiteral("businessAt"));
    note_ = new QTextEdit(this);
    note_->setObjectName(QStringLiteral("note"));
    form->addRow(QStringLiteral("单号"), numberLayout);
    form->addRow(type_ == DocumentType::Transfer ? QStringLiteral("发出公司库位")
        : type_ == DocumentType::Outbound ? QStringLiteral("出库工厂")
                                         : QStringLiteral("退回工厂"), source_);
    form->addRow(type_ == DocumentType::Transfer ? QStringLiteral("接收工厂")
        : type_ == DocumentType::Outbound ? QStringLiteral("最终目的地（Amazon 仓）")
                                         : QStringLiteral("接收公司库位"), destination_);
    form->addRow(QStringLiteral("业务时间"), businessAt_);
    if (type_ == DocumentType::Outbound) {
        outboundConfirmedBy_ = new QComboBox(this);
        outboundConfirmedBy_->setObjectName(QStringLiteral("outboundConfirmedById"));
        outboundConfirmedBy_->setToolTip(QStringLiteral("确认出库前必选；请先在基础资料的人员名单中录入姓名"));
        form->addRow(QStringLiteral("出库确认人"), outboundConfirmedBy_);
        connect(outboundConfirmedBy_, &QComboBox::activated, this, &LogisticsDialog::updateContentSize);
    }
    form->addRow(QStringLiteral("备注"), note_);
    layout->addLayout(form);
    if (type_ == DocumentType::Return) {
        auto *hint = new QLabel(QStringLiteral(
            "工厂退回公司属于内部流转，总库存不变；同一批次的不同型号共用工厂可用数量。"), this);
        hint->setWordWrap(true);
        layout->addWidget(hint);
    }

    lines_ = new QTableWidget(0, 3, this);
    lines_->setObjectName(QStringLiteral("lines"));
    lines_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lines_->setHorizontalHeaderLabels({QStringLiteral("批次"), QStringLiteral("数量"),
        type_ == DocumentType::Transfer ? QStringLiteral("调拨型号")
            : type_ == DocumentType::Outbound ? QStringLiteral("出库型号") : QStringLiteral("退回型号")});
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
        if (lines_->currentRow() >= 0) {
            lines_->removeRow(lines_->currentRow());
            updateContentSize();
        }
    });
    connect(save_, &QPushButton::clicked, this, [this] {
        if (save())
            accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(documentNo_, &QLineEdit::textEdited, this, &LogisticsDialog::updateContentSize);
    connect(destination_, &QComboBox::activated, this, &LogisticsDialog::updateContentSize);
}

void LogisticsDialog::updateContentSize()
{
    documentNo_->setToolTip(documentNo_->text());
    LayoutUtils::fitTableColumns(lines_);
    LayoutUtils::fitDialogToContents(this, QSize(1060, 680));
}

void LogisticsDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    updateContentSize();
}

void LogisticsDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::FontChange)
        QTimer::singleShot(0, this, &LogisticsDialog::updateContentSize);
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
        const auto sourceType = type_ == DocumentType::Transfer
            ? LocationType::Company : LocationType::Factory;
        const bool valid = location.type == sourceType && location.countsAsInventory;
        if (valid)
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
        const bool valid = location.id != sourceId &&
            (type_ == DocumentType::Outbound
                ? !location.countsAsInventory && location.type != LocationType::Company &&
                    location.type != LocationType::Factory
                : location.countsAsInventory && location.type ==
                    (type_ == DocumentType::Transfer ? LocationType::Factory : LocationType::Company));
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

Result<bool> LogisticsDialog::initialize(EntityId id, bool readOnly, EntityId preferredBatchId)
{
    id_ = id;
    readOnly_ = readOnly;
    if (outboundConfirmedBy_) {
        outboundConfirmedBy_->clear();
        if (!readOnly_) {
            const auto people = masterData_.enabledPersonnelResult();
            if (!people.ok()) {
                showError(people.error());
                return Result<bool>::failure(people.error());
            }
            outboundConfirmedBy_->addItem(people.value().isEmpty()
                ? QStringLiteral("请先到基础资料 → 人员名单录入") : QStringLiteral("请选择"), EntityId(0));
            for (const auto &person : people.value())
                outboundConfirmedBy_->addItem(person.name, person.id);
        }
    }
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
        batches_.clear();
        if (!readOnly_) {
            const auto batchResult = loadBatches(loaded.value().sourceLocationId);
            if (!batchResult.ok()) {
                showError(batchResult.error());
                return batchResult;
            }
        }
        if (outboundConfirmedBy_) {
            const auto &document = loaded.value();
            if (readOnly_) {
                outboundConfirmedBy_->addItem(document.outboundConfirmedByName.isEmpty()
                    ? QStringLiteral("未填写") : document.outboundConfirmedByName, document.outboundConfirmedById);
            } else {
                if (document.outboundConfirmedById > 0 &&
                    outboundConfirmedBy_->findData(document.outboundConfirmedById) < 0) {
                    outboundConfirmedBy_->addItem(document.outboundConfirmedByName + QStringLiteral("（已停用）"),
                                                 document.outboundConfirmedById);
                }
                outboundConfirmedBy_->setCurrentIndex(outboundConfirmedBy_->findData(document.outboundConfirmedById));
            }
        }
        originalNumber_ = loaded.value().documentNo;
        documentNo_->setText(originalNumber_);
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
            EntityId sourceId = source_->currentData().toLongLong();
            if (preferredBatchId > 0) {
                const auto balances = queries_.batchLocationBalances(preferredBatchId);
                if (!balances.ok()) {
                    showError(balances.error());
                    return Result<bool>::failure(balances.error());
                }
                for (const auto &balance : balances.value()) {
                    if (balance.quantity > 0 && source_->findData(balance.locationId) >= 0) {
                        sourceId = balance.locationId;
                        break;
                    }
                }
            }
            const auto sourceResult = setSourceLocation(sourceId);
            if (!sourceResult.ok())
                return sourceResult;
            if (preferredBatchId > 0 && std::any_of(
                    batches_.cbegin(), batches_.cend(), [preferredBatchId](const BatchChoice &choice) {
                        return choice.id == preferredBatchId;
                    })) {
                const auto line = addLine();
                if (!line.ok())
                    return line;
                const auto selected = setLineBatch(0, preferredBatchId);
                if (!selected.ok())
                    return selected;
            }
        }
    }

    if (!id)
        originalNumber_.clear();
    manualNumber_->setChecked(false);
    documentNo_->setText(originalNumber_);
    documentNo_->setReadOnly(true);
    manualNumber_->setVisible(!readOnly_);
    const QList<QWidget *> controls{documentNo_, manualNumber_, source_, destination_, businessAt_, note_,
                                    add_, remove_};
    for (QWidget *widget : controls)
        widget->setEnabled(!readOnly_);
    if (outboundConfirmedBy_)
        outboundConfirmedBy_->setEnabled(!readOnly_);
    for (int row = 0; row < lines_->rowCount(); ++row) {
        for (int column : {0, 2})
            lines_->cellWidget(row, column)->setEnabled(!readOnly_);
        qobject_cast<QLineEdit *>(lines_->cellWidget(row, 1))->setReadOnly(readOnly_);
    }
    save_->setVisible(!readOnly_);
    updateContentSize();
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
    const auto result = loadBatches(id);
    if (!result.ok()) {
        for (int row = 0; row < lines_->rowCount(); ++row) {
            for (int column : {0, 2}) {
                if (auto *combo = qobject_cast<QComboBox *>(lines_->cellWidget(row, column))) {
                    const QSignalBlocker blocker(combo);
                    combo->clear();
                }
            }
        }
        showError(result.error());
        updateContentSize();
        return result;
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
    updateContentSize();
    return Result<bool>::success(true);
}

Result<bool> LogisticsDialog::loadBatches(EntityId locationId)
{
    batches_.clear();
    if (type_ == DocumentType::Return) {
        const auto rows = service_.returnableLines(locationId);
        if (!rows.ok()) return Result<bool>::failure(rows.error());
        for (const auto &row : rows.value()) {
            batches_.append({row.batchId, row.productTypeId,
                QStringLiteral("%1 / 物料型号：%2 / 工厂可用 %3")
                    .arg(row.batchNo, row.materialModel.isEmpty() ? QStringLiteral("未填写") : row.materialModel)
                    .arg(row.quantity)});
        }
    } else {
        const auto rows = queries_.availableBatches(locationId);
        if (!rows.ok()) return Result<bool>::failure(rows.error());
        for (const auto &row : rows.value())
            batches_.append({row.batchId, row.productTypeId,
                QStringLiteral("%1 / %2 / 物料型号：%3 / %4")
                    .arg(row.batchNo, row.typeCode,
                         row.materialModel.isEmpty() ? QStringLiteral("未填写") : row.materialModel)
                    .arg(row.quantity)});
    }
    return Result<bool>::success(true);
}

Result<bool> LogisticsDialog::populateModels(int row, EntityId modelId,
                                           const LogisticsLine *persistedLine)
{
    auto *batch = qobject_cast<QComboBox *>(lines_->cellWidget(row, 0));
    auto *models = qobject_cast<QComboBox *>(lines_->cellWidget(row, 2));
    if (!models) {
        models = new QComboBox(lines_);
        lines_->setCellWidget(row, 2, models);
        connect(models, &QComboBox::currentIndexChanged, this, &LogisticsDialog::updateContentSize);
    }
    const QSignalBlocker blocker(models);
    models->clear();
    if (readOnly_ && persistedLine) {
        models->addItem(persistedLine->modelCodeSnapshot.isEmpty()
            ? QStringLiteral("未细分") : persistedLine->modelCodeSnapshot, modelId);
        return Result<bool>::success(true);
    }
    const EntityId batchId = batch->currentData().toLongLong();
    EntityId typeId = persistedLine ? persistedLine->productTypeId : 0;
    for (const auto &choice : batches_) {
        if (choice.id == batchId) typeId = choice.productTypeId;
    }
    if (typeId) {
        const auto choices = masterData_.enabledProductModelsForTypeResult(typeId);
        if (!choices.ok()) return Result<bool>::failure(choices.error());
        for (const auto &choice : choices.value())
            models->addItem(choice.modelCode, choice.id);
    }
    models->addItem(QStringLiteral("未细分 / 原材料"), EntityId(0));
    if (persistedLine && models->findData(modelId) < 0) {
        QString code = persistedLine->modelCodeSnapshot;
        if (code.isEmpty() && modelId > 0) {
            const auto historical = masterData_.productModelById(modelId);
            if (!historical.ok()) return Result<bool>::failure(historical.error());
            code = historical.value().modelCode;
        }
        models->addItem(code.isEmpty() ? QStringLiteral("未细分（历史）")
                                      : code + QStringLiteral("（已停用/历史）"), modelId);
    }
    const int selected = models->findData(modelId);
    if (modelId >= 0) {
        models->setCurrentIndex(selected);
        if (selected < 0) {
            return Result<bool>::failure({QStringLiteral("not_found"), QStringLiteral("型号不可用"),
                                          QStringLiteral("lines[%1].productModelId").arg(row)});
        }
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
    const auto result = populateLine(lines_->rowCount(), {});
    updateContentSize();
    return result;
}

Result<bool> LogisticsDialog::populateLine(int row, const LogisticsLineDraft &line,
                                            const LogisticsLine *persistedLine)
{
    if (row >= lines_->rowCount())
        lines_->insertRow(row);

    auto *batch = qobject_cast<QComboBox *>(lines_->cellWidget(row, 0));
    if (!batch) {
        batch = new QComboBox(lines_);
        lines_->setCellWidget(row, 0, batch);
        connect(batch, &QComboBox::currentIndexChanged, this, [this, batch] {
            for (int currentRow = 0; currentRow < lines_->rowCount(); ++currentRow) {
                if (lines_->cellWidget(currentRow, 0) != batch) continue;
                const auto result = populateModels(currentRow);
                if (!result.ok()) showError(result.error());
                updateContentSize();
                break;
            }
        });
    }
    batch->blockSignals(true);
    batch->clear();
    for (const auto &choice : batches_)
        batch->addItem(choice.text, choice.id);
    if (line.batchId > 0 && batch->findData(line.batchId) < 0) {
        const QString text = persistedLine
            ? QStringLiteral("%1 / %2 / 物料型号：%3 (历史批次)")
                  .arg(persistedLine->batchNoSnapshot, persistedLine->typeCodeSnapshot,
                       persistedLine->materialModel.isEmpty() ? QStringLiteral("未填写")
                                                              : persistedLine->materialModel)
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
        connect(quantity, &QLineEdit::textEdited, this, &LogisticsDialog::updateContentSize);
    }
    quantity->setText(line.quantity > 0 ? QString::number(line.quantity)
                                        : (line.batchId > 0 ? QStringLiteral("0")
                                                            : QStringLiteral("1")));
    const auto models = populateModels(row, line.batchId > 0 ? line.productModelId : -1, persistedLine);
    if (!models.ok()) showError(models.error());
    return models;
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
    manualNumber_->setChecked(!value.documentNo.trimmed().isEmpty());
    documentNo_->setText(value.documentNo);
    businessAt_->setDateTime(value.businessAt.isValid() ? value.businessAt
                                                         : QDateTime::currentDateTime());
    note_->setPlainText(value.note);
    if (outboundConfirmedBy_) {
        const int index = outboundConfirmedBy_->findData(value.outboundConfirmedById);
        if (index < 0)
            return Result<bool>::failure({QStringLiteral("not_found"), QStringLiteral("出库确认人不可用"),
                                          QStringLiteral("outboundConfirmedById")});
        outboundConfirmedBy_->setCurrentIndex(index);
    }
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
    updateContentSize();
    return Result<bool>::success(true);
}

LogisticsDraft LogisticsDialog::draft() const
{
    LogisticsDraft value{documentNo_->text(), type_, source_->currentData().toLongLong(),
                         destination_->currentData().toLongLong(), businessAt_->dateTime(),
                         note_->toPlainText(), {},
                         outboundConfirmedBy_ ? outboundConfirmedBy_->currentData().toLongLong() : 0};
    for (int row = 0; row < lines_->rowCount(); ++row) {
        auto *batch = qobject_cast<QComboBox *>(lines_->cellWidget(row, 0));
        auto *quantity = qobject_cast<QLineEdit *>(lines_->cellWidget(row, 1));
        bool ok = false;
        const qint64 amount = quantity ? quantity->text().toLongLong(&ok) : 0;
        auto *models = qobject_cast<QComboBox *>(lines_->cellWidget(row, 2));
        value.lines.append({batch ? batch->currentData().toLongLong() : 0,
                            ok ? amount : 0,
                            models && models->currentIndex() >= 0 ? models->currentData().toLongLong() : -1});
    }
    return value;
}

bool LogisticsDialog::save()
{
    if (manualNumber_->isChecked() && documentNo_->text().trimmed().isEmpty()) {
        showError({QStringLiteral("required"), QStringLiteral("手动单号不能为空"),
                   QStringLiteral("documentNo")});
        return false;
    }
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
    if (field == QStringLiteral("outboundConfirmedById"))
        return outboundConfirmedBy_;
    const QRegularExpression lineField(QStringLiteral("^lines\\[(\\d+)\\]\\.(batchId|quantity|productModelId)$"));
    const auto match = lineField.match(field);
    if (match.hasMatch()) {
        const int row = match.captured(1).toInt();
        const int column = match.captured(2) == QStringLiteral("batchId") ? 0
            : match.captured(2) == QStringLiteral("quantity") ? 1 : 2;
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
