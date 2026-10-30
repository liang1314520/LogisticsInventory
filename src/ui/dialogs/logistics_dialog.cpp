#include "logistics_dialog.h"

#include "core/packaging.h"
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
#include <QHeaderView>
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

namespace {
constexpr int unitsPerBoxRole = Qt::UserRole + 1;
constexpr int boxesPerCartonRole = Qt::UserRole + 2;
}

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
        outboundConfirmedBy_->setToolTip(QStringLiteral("保存前必选；请先在基础资料的人员名单中录入姓名"));
        form->addRow(QStringLiteral("出库确认人"), outboundConfirmedBy_);
        connect(outboundConfirmedBy_, &QComboBox::activated, this, &LogisticsDialog::updateContentSize);
    }
    form->addRow(QStringLiteral("备注"), note_);
    layout->addLayout(form);
    saveHint_ = new QLabel(this);
    saveHint_->setObjectName(QStringLiteral("saveHint"));
    saveHint_->setWordWrap(true);
    layout->addWidget(saveHint_);
    if (type_ == DocumentType::Return) {
        auto *hint = new QLabel(QStringLiteral(
            "工厂退回公司属于内部流转，总库存不变；同一批次的不同型号共用工厂账面库存，"
            "允许零库存或负库存补录。"), this);
        hint->setWordWrap(true);
        layout->addWidget(hint);
    }

    const bool packaging = type_ != DocumentType::Return;
    lines_ = new QTableWidget(0, packaging ? 6 : 3, this);
    lines_->setObjectName(QStringLiteral("lines"));
    lines_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    QStringList headers{QStringLiteral("批次"),
        packaging ? QStringLiteral("散个数") : QStringLiteral("数量"),
        type_ == DocumentType::Transfer ? QStringLiteral("调拨型号")
            : type_ == DocumentType::Outbound ? QStringLiteral("出库型号") : QStringLiteral("退回型号")};
    if (packaging)
        headers.append({QStringLiteral("箱数"), QStringLiteral("盒数"), QStringLiteral("总个数")});
    lines_->setHorizontalHeaderLabels(headers);
    if (packaging) {
        auto *header = lines_->horizontalHeader();
        header->moveSection(header->visualIndex(2), 1);
        header->moveSection(header->visualIndex(3), 2);
        header->moveSection(header->visualIndex(4), 3);
    }
    layout->addWidget(lines_);
    if (packaging) {
        auto *hint = new QLabel(QStringLiteral(
            "同一行可填写箱数＋盒数＋散个数，库存按总个数记账。包装规格在基础资料的调拨/出库型号中设置；"
            "悬停数量可查看换算规格。未设置规格时仅填写散个数。"), this);
        hint->setWordWrap(true);
        layout->addWidget(hint);
    }

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

PackagingSpec LogisticsDialog::linePackagingSpec(int row) const
{
    auto *batch = qobject_cast<QComboBox *>(lines_->cellWidget(row, 0));
    auto *models = qobject_cast<QComboBox *>(lines_->cellWidget(row, 2));
    const EntityId batchId = batch->currentData().toLongLong();
    const EntityId modelId = models->currentData().toLongLong();
    for (const auto &line : original_.lines) {
        if (line.batchId == batchId && line.productModelId == modelId && line.packaging)
            return line.packagingSpec;
    }
    return {models->currentData(unitsPerBoxRole).toLongLong(),
            models->currentData(boxesPerCartonRole).toLongLong()};
}

Result<PackagingQuantities> LogisticsDialog::lineQuantities(int row) const
{
    const auto read = [this, row](int column, const QString &field) {
        auto *edit = qobject_cast<QLineEdit *>(lines_->cellWidget(row, column));
        if (edit->text().isEmpty())
            return Result<qint64>::success(0);
        bool ok = false;
        const qint64 value = edit->text().toLongLong(&ok);
        if (!ok || !edit->hasAcceptableInput())
            return Result<qint64>::failure({QStringLiteral("invalid_quantity"),
                QStringLiteral("数量必须为范围内的非负整数"),
                QStringLiteral("lines[%1].%2").arg(row).arg(field)});
        return Result<qint64>::success(value);
    };
    const auto loose = read(1, type_ == DocumentType::Return ? QStringLiteral("quantity")
                                                           : QStringLiteral("looseQuantity"));
    if (!loose.ok()) return Result<PackagingQuantities>::failure(loose.error());
    PackagingQuantities quantities{0, 0, loose.value()};
    if (type_ != DocumentType::Return) {
        const auto cartons = read(3, QStringLiteral("cartonCount"));
        if (!cartons.ok()) return Result<PackagingQuantities>::failure(cartons.error());
        const auto boxes = read(4, QStringLiteral("boxCount"));
        if (!boxes.ok()) return Result<PackagingQuantities>::failure(boxes.error());
        quantities.cartonCount = cartons.value();
        quantities.boxCount = boxes.value();
    }
    return Result<PackagingQuantities>::success(quantities);
}

void LogisticsDialog::updateLineQuantity(int row)
{
    if (type_ == DocumentType::Return)
        return;
    const PackagingSpec spec = linePackagingSpec(row);
    auto *total = qobject_cast<QLineEdit *>(lines_->cellWidget(row, 5));
    const auto quantities = lineQuantities(row);
    const auto quantity = quantities.ok()
        ? calculatePackagingQuantity(quantities.value(), spec)
        : Result<qint64>::failure(quantities.error());
    total->setText(quantity.ok() ? QString::number(quantity.value()) : QStringLiteral("数量无效"));
    QString hint;
    if (!quantity.ok()) {
        hint = quantity.error().message;
    } else if (spec.unitsPerBox > 0) {
        hint = QStringLiteral("1盒＝%1个；1箱＝%2盒＝%3个；原单使用保存时的规格")
            .arg(spec.unitsPerBox).arg(spec.boxesPerCarton)
            .arg(calculatePackagingQuantity({1, 0, 0}, spec).value());
    } else {
        hint = QStringLiteral("未设置包装规格，只能按个填写");
    }
    for (int column : {1, 3, 4, 5})
        lines_->cellWidget(row, column)->setToolTip(hint);
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
    if (preserveHistorical && selected > 0 && selected != sourceId &&
        destination_->findData(selected) < 0) {
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
    preferredBatchId_ = !id && !readOnly &&
        (type_ == DocumentType::Transfer || type_ == DocumentType::Outbound)
        ? preferredBatchId : 0;
    original_ = {};
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
        original_ = loaded.value();
        const bool cancelled = loaded.value().status == DocumentStatus::Cancelled;
        save_->setText(cancelled ? QStringLiteral("保存并恢复生效") : QStringLiteral("保存修改"));
        saveHint_->setText(cancelled
            ? QStringLiteral("此记录已作废，保存修改后将恢复生效并重新计入库存。允许负库存，请核对或补录。")
            : QStringLiteral("保存将更新原记录及其库存影响。允许负库存，请核对或补录。"));

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
        save_->setText(QStringLiteral("保存"));
        saveHint_->setText(QStringLiteral("保存后立即生效。允许负库存，请核对或补录相关记录。"));
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
            if (preferredBatchId_ > 0 || (preferredBatchId > 0 && std::any_of(
                    batches_.cbegin(), batches_.cend(), [preferredBatchId](const BatchChoice &choice) {
                        return choice.id == preferredBatchId;
                    }))) {
                const auto line = addLine();
                if (!line.ok())
                    return line;
                if (preferredBatchId_ == 0) {
                    const auto selected = setLineBatch(0, preferredBatchId);
                    if (!selected.ok())
                        return selected;
                }
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
        for (int column : {1, 3, 4}) {
            if (column < lines_->columnCount())
                qobject_cast<QLineEdit *>(lines_->cellWidget(row, column))->setReadOnly(readOnly_);
        }
    }
    save_->setVisible(!readOnly_);
    saveHint_->setVisible(!readOnly_);
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
    const QSignalBlocker sourceBlocker(source_);
    source_->setCurrentIndex(sourceIndex);
    const EntityId destinationId = destination_->currentData().toLongLong();
    const auto destinations = refreshDestinations(destinationId,
        destinationId == original_.destinationLocationId);
    if (!destinations.ok())
        return destinations;
    const auto result = loadBatches(id);
    if (!result.ok()) {
        showError(result.error());
        return result;
    }
    for (int row = 0; row < lines_->rowCount(); ++row) {
        auto *batch = qobject_cast<QComboBox *>(lines_->cellWidget(row, 0));
        const EntityId batchId = batch->currentData().toLongLong();
        const QString text = batch->currentText();
        const QSignalBlocker blocker(batch);
        batch->clear();
        for (const auto &choice : batches_)
            batch->addItem(choice.text, choice.id);
        if (batchId > 0 && batch->findData(batchId) < 0)
            batch->addItem(text, batchId);
        batch->setCurrentIndex(batch->findData(batchId));
    }
    updateContentSize();
    return Result<bool>::success(true);
}

Result<bool> LogisticsDialog::loadBatches(EntityId locationId)
{
    const auto rows = queries_.availableBatches(locationId);
    if (!rows.ok()) return Result<bool>::failure(rows.error());
    QVector<BatchChoice> choices;
    for (const auto &row : rows.value())
        choices.append({row.batchId, row.productTypeId,
            QStringLiteral("%1 / %2 / 物料型号：%3 / 账面库存 %4")
                .arg(row.batchNo, row.typeCode,
                     row.materialModel.isEmpty() ? QStringLiteral("未填写") : row.materialModel)
                .arg(row.quantity)});
    for (const auto &line : original_.lines) {
        if (std::any_of(choices.cbegin(), choices.cend(), [&](const BatchChoice &choice) {
                return choice.id == line.batchId;
            }))
            continue;
        const auto balance = queries_.balance(line.batchId, locationId);
        if (!balance.ok()) return Result<bool>::failure(balance.error());
        choices.append({line.batchId, line.productTypeId,
            QStringLiteral("%1 / %2 / 物料型号：%3 / 账面库存 %4（原记录批次）")
                .arg(line.batchNoSnapshot, line.typeCodeSnapshot,
                     line.materialModel.isEmpty() ? QStringLiteral("未填写") : line.materialModel)
                .arg(balance.value())});
    }
    batches_ = choices;
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
        connect(models, &QComboBox::currentIndexChanged, this, [this, models] {
            for (int currentRow = 0; currentRow < lines_->rowCount(); ++currentRow) {
                if (lines_->cellWidget(currentRow, 2) != models) continue;
                updateLineQuantity(currentRow);
                updateContentSize();
                break;
            }
        });
    }
    const QSignalBlocker blocker(models);
    models->clear();
    const auto appendModel = [models](const QString &text, EntityId id, const PackagingSpec &spec) {
        models->addItem(text, id);
        models->setItemData(models->count() - 1, spec.unitsPerBox, unitsPerBoxRole);
        models->setItemData(models->count() - 1, spec.boxesPerCarton, boxesPerCartonRole);
    };
    if (readOnly_ && persistedLine) {
        appendModel(persistedLine->modelCodeSnapshot.isEmpty()
            ? QStringLiteral("未细分") : persistedLine->modelCodeSnapshot,
            modelId, persistedLine->packagingSpec);
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
            appendModel(choice.modelCode, choice.id, choice.packagingSpec);
    }
    models->addItem(QStringLiteral("未细分 / 原材料"), EntityId(0));
    for (const auto &line : original_.lines) {
        if (line.batchId != batchId || models->findData(line.productModelId) >= 0)
            continue;
        PackagingSpec spec = line.packagingSpec;
        if (!line.packaging && line.productModelId > 0) {
            const auto historical = masterData_.productModelById(line.productModelId);
            if (!historical.ok()) return Result<bool>::failure(historical.error());
            spec = historical.value().packagingSpec;
        }
        appendModel(line.modelCodeSnapshot + QStringLiteral("（原记录型号）"), line.productModelId, spec);
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
    LogisticsLineInput line;
    if (preferredBatchId_ > 0) {
        const bool available = std::any_of(batches_.cbegin(), batches_.cend(),
            [this](const BatchChoice &choice) { return choice.id == preferredBatchId_; });
        if (!available)
            return Result<bool>::failure({QStringLiteral("not_found"),
                QStringLiteral("当前批次不可用，请重新选择批次"), QStringLiteral("lines")});
        line.batchId = preferredBatchId_;
        line.quantity = 1;
        line.productModelId = -1;
    }
    const int row = lines_->rowCount();
    const auto result = populateLine(row, line);
    if (result.ok() && type_ != DocumentType::Return && linePackagingSpec(row).unitsPerBox > 0) {
        auto *quantity = qobject_cast<QLineEdit *>(lines_->cellWidget(row, 1));
        quantity->setText(QStringLiteral("0"));
    }
    updateContentSize();
    return result;
}

Result<bool> LogisticsDialog::populateLine(int row, const LogisticsLineInput &line,
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
                updateLineQuantity(currentRow);
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

    for (int column : {1, 3, 4}) {
        if (type_ == DocumentType::Return && column != 1)
            continue;
        auto *quantity = qobject_cast<QLineEdit *>(lines_->cellWidget(row, column));
        if (!quantity) {
            quantity = new QLineEdit(lines_);
            quantity->setObjectName(column == 1 ? QStringLiteral("looseQuantity")
                : column == 3 ? QStringLiteral("cartonCount") : QStringLiteral("boxCount"));
            quantity->setValidator(new QRegularExpressionValidator(QRegularExpression(
                type_ == DocumentType::Return ? QStringLiteral("[1-9][0-9]{0,18}")
                                               : QStringLiteral("[0-9]{0,19}")), quantity));
            lines_->setCellWidget(row, column, quantity);
            connect(quantity, &QLineEdit::textChanged, this, [this, quantity, column] {
                for (int currentRow = 0; currentRow < lines_->rowCount(); ++currentRow) {
                    if (lines_->cellWidget(currentRow, column) != quantity) continue;
                    updateLineQuantity(currentRow);
                    updateContentSize();
                    break;
                }
            });
        }
        const QSignalBlocker blocker(quantity);
        if (column == 1) {
            quantity->setText(line.packaging ? QString::number(line.packaging->looseQuantity)
                : line.quantity > 0 ? QString::number(line.quantity)
                : line.batchId > 0 ? QStringLiteral("0") : QStringLiteral("1"));
        } else {
            const qint64 count = !line.packaging ? 0
                : column == 3 ? line.packaging->cartonCount : line.packaging->boxCount;
            quantity->setText(QString::number(count));
        }
        quantity->setReadOnly(readOnly_);
    }
    if (type_ != DocumentType::Return && !lines_->cellWidget(row, 5)) {
        auto *total = new QLineEdit(lines_);
        total->setObjectName(QStringLiteral("totalQuantity"));
        total->setReadOnly(true);
        lines_->setCellWidget(row, 5, total);
    }
    const auto models = populateModels(row, line.batchId > 0 ? line.productModelId : -1, persistedLine);
    if (!models.ok()) {
        showError(models.error());
        return models;
    }
    updateLineQuantity(row);
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

Result<bool> LogisticsDialog::setInput(const LogisticsInput &value)
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

LogisticsInput LogisticsDialog::input() const
{
    LogisticsInput value{documentNo_->text(), type_, source_->currentData().toLongLong(),
                         destination_->currentData().toLongLong(), businessAt_->dateTime(),
                         note_->toPlainText(), {},
                         outboundConfirmedBy_ ? outboundConfirmedBy_->currentData().toLongLong() : 0};
    for (int row = 0; row < lines_->rowCount(); ++row) {
        auto *batch = qobject_cast<QComboBox *>(lines_->cellWidget(row, 0));
        auto *models = qobject_cast<QComboBox *>(lines_->cellWidget(row, 2));
        const auto quantities = lineQuantities(row);
        LogisticsLineInput line{batch->currentData().toLongLong(),
            quantities.ok() ? quantities.value().looseQuantity : -1,
            models->currentIndex() >= 0 ? models->currentData().toLongLong() : -1};
        if (type_ != DocumentType::Return && quantities.ok()) {
            const auto spec = linePackagingSpec(row);
            if (spec.unitsPerBox != 0 || spec.boxesPerCarton != 0 ||
                quantities.value().cartonCount != 0 || quantities.value().boxCount != 0) {
                line.packaging = quantities.value();
                const auto total = calculatePackagingQuantity(*line.packaging, spec);
                line.quantity = total.ok() ? total.value() : -1;
            }
        }
        value.lines.append(line);
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
    for (int row = 0; row < lines_->rowCount(); ++row) {
        const auto quantities = lineQuantities(row);
        if (!quantities.ok()) {
            showError(quantities.error());
            return false;
        }
        if (type_ != DocumentType::Return) {
            const auto total = calculatePackagingQuantity(quantities.value(), linePackagingSpec(row));
            if (!total.ok()) {
                auto failure = total.error();
                failure.field = QStringLiteral("lines[%1].%2").arg(row).arg(failure.field);
                showError(failure);
                return false;
            }
        }
    }
    const auto value = input();
    const auto result = id_
        ? service_.update(id_, value)
        : [&] {
              const auto created = service_.create(value);
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
    const QRegularExpression lineField(QStringLiteral(
        "^lines\\[(\\d+)\\]\\.(batchId|quantity|productModelId|cartonCount|boxCount|looseQuantity|unitsPerBox|boxesPerCarton)$"));
    const auto match = lineField.match(field);
    if (match.hasMatch()) {
        const int row = match.captured(1).toInt();
        const QString name = match.captured(2);
        const int column = name == QStringLiteral("batchId") ? 0
            : name == QStringLiteral("quantity") || name == QStringLiteral("looseQuantity") ? 1
            : name == QStringLiteral("cartonCount") ? 3
            : name == QStringLiteral("boxCount") ? 4 : 2;
        if (row >= 0 && row < lines_->rowCount() && column < lines_->columnCount())
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
