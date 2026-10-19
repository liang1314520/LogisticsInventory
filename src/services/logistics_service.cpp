#include "logistics_service.h"

#include <QHash>
#include <QSet>

#include <limits>

namespace {
ServiceError error(const QString &code, const QString &message, const QString &field = {})
{
    return {code, message, field};
}
ServiceError invalidStatus()
{
    return error(QStringLiteral("invalid_status"), QStringLiteral("单据状态不允许此操作"),
                 QStringLiteral("status"));
}
ServiceError invalidMasterData(const QString &field)
{
    return error(QStringLiteral("invalid_master_data"), QStringLiteral("主数据不可用于确认"), field);
}
QString stockKey(EntityId batchId, EntityId locationId)
{
    return QStringLiteral("%1:%2").arg(batchId).arg(locationId);
}
}

LogisticsService::LogisticsService(Database &database, MasterDataRepository &masterData,
                                   DocumentRepository &documents,
                                   InventoryRepository &inventory)
    : database_(database), masterData_(masterData), documents_(documents), inventory_(inventory)
{
}

ServiceError LogisticsService::validateDraft(const LogisticsDraft &draft, bool requireNumber) const
{
    if (requireNumber && draft.documentNo.trimmed().isEmpty())
        return error(QStringLiteral("required"), QStringLiteral("必填字段不能为空"),
                     QStringLiteral("documentNo"));
    if (draft.sourceLocationId <= 0)
        return error(QStringLiteral("invalid"), QStringLiteral("字段值无效"),
                     QStringLiteral("sourceLocationId"));
    if (draft.destinationLocationId <= 0)
        return error(QStringLiteral("invalid"), QStringLiteral("字段值无效"),
                     QStringLiteral("destinationLocationId"));
    if (draft.sourceLocationId == draft.destinationLocationId)
        return error(QStringLiteral("invalid"), QStringLiteral("字段值无效"),
                     QStringLiteral("destinationLocationId"));
    if (!draft.businessAt.isValid())
        return error(QStringLiteral("invalid"), QStringLiteral("字段值无效"),
                     QStringLiteral("businessAt"));
    if (draft.lines.isEmpty())
        return error(QStringLiteral("required"), QStringLiteral("至少需要一条明细"),
                     QStringLiteral("lines"));
    if (draft.type != DocumentType::Transfer && draft.type != DocumentType::Outbound &&
        draft.type != DocumentType::Return)
        return error(QStringLiteral("invalid"), QStringLiteral("单据类型无效"), QStringLiteral("type"));
    if (draft.type == DocumentType::Outbound) {
        if (draft.outboundConfirmedById < 0)
            return error(QStringLiteral("invalid"), QStringLiteral("出库确认人无效"),
                         QStringLiteral("outboundConfirmedById"));
        if (draft.outboundConfirmedById > 0) {
            const auto person = masterData_.personnelById(draft.outboundConfirmedById);
            if (!person.ok())
                return error(person.error().code, person.error().message,
                             QStringLiteral("outboundConfirmedById"));
            if (!person.value().enabled)
                return error(QStringLiteral("invalid_master_data"),
                    QStringLiteral("出库确认人已停用，请编辑草稿重新选择"),
                    QStringLiteral("outboundConfirmedById"));
        }
    }
    const auto source = masterData_.locationById(draft.sourceLocationId);
    if (!source.ok())
        return error(source.error().code, source.error().message, QStringLiteral("sourceLocationId"));
    const auto sourceType = draft.type == DocumentType::Transfer
        ? LocationType::Company : LocationType::Factory;
    if (!source.value().enabled || source.value().type != sourceType ||
        !source.value().countsAsInventory)
        return error(QStringLiteral("invalid_master_data"),
            draft.type == DocumentType::Transfer
                ? QStringLiteral("调拨来源必须是已启用且计库存的公司库位")
                : QStringLiteral("出库或退回来源必须是已启用且计库存的工厂"),
            QStringLiteral("sourceLocationId"));
    const auto destination = masterData_.locationById(draft.destinationLocationId);
    if (!destination.ok())
        return error(destination.error().code, destination.error().message,
                     QStringLiteral("destinationLocationId"));
    bool destinationValid = destination.value().enabled;
    QString destinationMessage;
    switch (draft.type) {
    case DocumentType::Transfer:
        destinationValid = destinationValid && destination.value().countsAsInventory &&
            destination.value().type == LocationType::Factory;
        destinationMessage = QStringLiteral("调拨目的地必须是已启用且计库存的工厂");
        break;
    case DocumentType::Outbound:
        destinationValid = destinationValid && !destination.value().countsAsInventory &&
            destination.value().type != LocationType::Company &&
            destination.value().type != LocationType::Factory;
        destinationMessage = QStringLiteral("出库目的地必须是已启用且不计库存的外部地点（如海外 Amazon 仓）");
        break;
    case DocumentType::Return:
        destinationValid = destinationValid && destination.value().countsAsInventory &&
            destination.value().type == LocationType::Company;
        destinationMessage = QStringLiteral("退回目的地必须是已启用且计库存的公司库位");
        break;
    }
    if (!destinationValid)
        return error(QStringLiteral("invalid_master_data"), destinationMessage,
                     QStringLiteral("destinationLocationId"));
    QSet<QString> choices;
    for (int i = 0; i < draft.lines.size(); ++i) {
        const LogisticsLineDraft &line = draft.lines.at(i);
        const QString prefix = QStringLiteral("lines[%1].").arg(i);
        if (line.batchId <= 0)
            return error(QStringLiteral("invalid"), QStringLiteral("明细批次无效"), prefix + QStringLiteral("batchId"));
        if (line.productModelId < 0)
            return error(QStringLiteral("invalid"), QStringLiteral("型号无效"), prefix + QStringLiteral("productModelId"));
        const QString key = QStringLiteral("%1:%2").arg(line.batchId).arg(line.productModelId);
        if (choices.contains(key))
            return error(QStringLiteral("duplicate_batch"), QStringLiteral("同一批次的同一型号请合并为一行"),
                         prefix + QStringLiteral("batchId"));
        choices.insert(key);
        if (line.quantity <= 0)
            return error(QStringLiteral("invalid"), QStringLiteral("明细数量无效"), prefix + QStringLiteral("quantity"));
    }
    return {};
}

ServiceError LogisticsService::validateDocument(const LogisticsDocument &document) const
{
    LogisticsDraft draft;
    draft.documentNo = document.documentNo;
    draft.type = document.type;
    draft.sourceLocationId = document.sourceLocationId;
    draft.destinationLocationId = document.destinationLocationId;
    draft.businessAt = document.businessAt;
    draft.note = document.note;
    draft.outboundConfirmedById = document.outboundConfirmedById;
    for (const LogisticsLine &line : document.lines)
        draft.lines.append({line.batchId, line.quantity, line.productModelId});
    return validateDraft(draft);
}

Result<EntityId> LogisticsService::createDraft(const LogisticsDraft &draft)
{
    const ServiceError validation = validateDraft(draft, false);
    if (!validation.code.isEmpty())
        return Result<EntityId>::failure(validation);
    LogisticsDraft normalized = draft;
    normalized.documentNo = normalized.documentNo.trimmed();
    normalized.note = normalized.note.trimmed();
    if (normalized.type != DocumentType::Outbound)
        normalized.outboundConfirmedById = 0;
    return documents_.insertLogisticsDraft(normalized);
}

Result<bool> LogisticsService::updateDraft(EntityId id, const LogisticsDraft &draft)
{
    const ServiceError validation = validateDraft(draft, false);
    if (!validation.code.isEmpty())
        return Result<bool>::failure(validation);
    LogisticsDraft normalized = draft;
    normalized.documentNo = normalized.documentNo.trimmed();
    normalized.note = normalized.note.trimmed();
    if (normalized.type != DocumentType::Outbound)
        normalized.outboundConfirmedById = 0;
    ServiceError operationError;
    QString transactionError;
    const bool committed = database_.transaction(Database::TransactionMode::Immediate, [&] {
        const auto updated = documents_.updateLogisticsDraft(id, normalized);
        if (!updated.ok()) {
            operationError = updated.error();
            return false;
        }
        return true;
    }, &transactionError);
    if (!committed) {
        if (operationError.code.isEmpty())
            operationError = error(QStringLiteral("database"), transactionError);
        return Result<bool>::failure(operationError);
    }
    return Result<bool>::success(true);
}

Result<LogisticsDocument> LogisticsService::byId(EntityId id) const
{
    return documents_.logisticsById(id);
}

Result<bool> LogisticsService::cancel(EntityId id)
{
    ServiceError operationError;
    QString transactionError;
    const bool committed = database_.transaction(Database::TransactionMode::Immediate, [&] {
        const auto documentResult = documents_.logisticsById(id);
        if (!documentResult.ok()) {
            operationError = documentResult.error();
            return false;
        }
        const LogisticsDocument &document = documentResult.value();
        if (document.status != DocumentStatus::Confirmed) {
            operationError = invalidStatus();
            return false;
        }
        if (document.type == DocumentType::Outbound) {
            QSet<EntityId> checkedBatches;
            for (const auto &line : document.lines) {
                if (checkedBatches.contains(line.batchId))
                    continue;
                checkedBatches.insert(line.batchId);
                const auto related = documents_.logisticsForBatch(line.batchId);
                if (!related.ok()) {
                    operationError = related.error();
                    return false;
                }
                for (const auto &item : related.value()) {
                    if (item.type == DocumentType::Return && item.status == DocumentStatus::Confirmed &&
                        item.sourceLocationId == document.destinationLocationId) {
                        operationError = error(QStringLiteral("return_dependency"),
                            QStringLiteral("出库目的地已有该批次的退回，请先取消相关退回单"));
                        return false;
                    }
                }
            }
        }
        const auto originals = inventory_.unreversedEntriesForSource(QStringLiteral("Logistics"), id);
        if (!originals.ok()) {
            operationError = originals.error();
            return false;
        }
        if (originals.value().isEmpty()) {
            operationError = error(QStringLiteral("invalid_state"),
                                   QStringLiteral("未找到可冲销的物流流水"));
            return false;
        }
        QHash<QString, qint64> resulting;
        for (const InventoryEntry &entry : originals.value()) {
            const QString key = stockKey(entry.batchId, entry.locationId);
            if (!resulting.contains(key)) {
                const auto quantity = inventory_.quantityResult(entry.batchId, entry.locationId);
                if (!quantity.ok()) {
                    operationError = quantity.error();
                    return false;
                }
                resulting.insert(key, quantity.value());
            }
            if (entry.quantityDelta > 0 && resulting.value(key) < entry.quantityDelta) {
                operationError = error(QStringLiteral("stock_dependency"),
                                       QStringLiteral("取消将导致库存为负，存在后续库存依赖"));
                return false;
            }
            if (entry.quantityDelta < 0 &&
                resulting.value(key) > std::numeric_limits<qint64>::max() + entry.quantityDelta) {
                operationError = error(QStringLiteral("quantity_overflow"),
                                       QStringLiteral("取消后的库存数量超出范围"));
                return false;
            }
            resulting[key] -= entry.quantityDelta;
        }
        for (const InventoryEntry &entry : originals.value()) {
            const auto reversal = inventory_.insertEntry(
                entry.batchId, entry.locationId, QStringLiteral("Logistics"), id,
                entry.sourceLineId, -entry.quantityDelta, document.businessAt, entry.id);
            if (!reversal.ok()) {
                operationError = reversal.error();
                return false;
            }
        }
        const auto cancelled = documents_.cancelLogistics(id, QDateTime::currentDateTimeUtc());
        if (!cancelled.ok()) {
            operationError = cancelled.error();
            return false;
        }
        return true;
    }, &transactionError);
    if (!committed) {
        if (operationError.code.isEmpty())
            operationError = error(QStringLiteral("database"), transactionError);
        return Result<bool>::failure(operationError);
    }
    return Result<bool>::success(true);
}

Result<bool> LogisticsService::confirm(EntityId id)
{
    ServiceError operationError;
    QString transactionError;
    const bool committed = database_.transaction(Database::TransactionMode::Immediate, [&] {
        const auto documentResult = documents_.logisticsById(id);
        if (!documentResult.ok()) {
            operationError = documentResult.error();
            return false;
        }
        const LogisticsDocument &document = documentResult.value();
        if (document.status != DocumentStatus::Draft) {
            operationError = invalidStatus();
            return false;
        }
        const ServiceError validation = validateDocument(document);
        if (!validation.code.isEmpty()) {
            operationError = validation;
            return false;
        }
        const auto source = masterData_.locationById(document.sourceLocationId);
        const auto destination = masterData_.locationById(document.destinationLocationId);
        if (!source.ok() || !destination.ok()) {
            const bool sourceFailed = !source.ok();
            const ServiceError original = sourceFailed ? source.error() : destination.error();
            operationError = error(original.code, original.message,
                                   sourceFailed ? QStringLiteral("sourceLocationId")
                                                : QStringLiteral("destinationLocationId"));
            return false;
        }
        QString outboundConfirmedByName;
        if (document.type == DocumentType::Outbound) {
            if (document.outboundConfirmedById == 0) {
                operationError = error(QStringLiteral("required"),
                    QStringLiteral("请先编辑草稿选择出库确认人，再确认出库"),
                    QStringLiteral("outboundConfirmedById"));
                return false;
            }
            const auto person = masterData_.personnelById(document.outboundConfirmedById);
            if (!person.ok()) {
                operationError = error(person.error().code, person.error().message,
                                       QStringLiteral("outboundConfirmedById"));
                return false;
            }
            outboundConfirmedByName = person.value().name;
        }
        const bool internalMovement = document.type != DocumentType::Outbound;
        QHash<EntityId, qint64> requiredByBatch;
        QHash<EntityId, int> firstLineByBatch;
        struct ConfirmedLineData {
            InboundBatch batch;
            ProductType productType;
            ProductCategory category;
            QString modelCode;
        };
        QVector<ConfirmedLineData> confirmedLines;
        confirmedLines.reserve(document.lines.size());
        for (int i = 0; i < document.lines.size(); ++i) {
            const LogisticsLine &line = document.lines.at(i);
            const auto batch = documents_.inboundById(line.batchId);
            if (!batch.ok()) {
                operationError = error(batch.error().code, batch.error().message,
                                       QStringLiteral("lines[%1].batchId").arg(i));
                return false;
            }
            const auto productType = masterData_.productTypeById(batch.value().productTypeId);
            if (!productType.ok()) {
                operationError = error(productType.error().code, productType.error().message,
                                       QStringLiteral("lines[%1].batchId").arg(i));
                return false;
            }
            const auto category = masterData_.categoryById(productType.value().categoryId);
            if (!category.ok()) {
                operationError = error(category.error().code, category.error().message,
                                       QStringLiteral("lines[%1].batchId").arg(i));
                return false;
            }
            if (batch.value().status != DocumentStatus::Confirmed ||
                !productType.value().enabled || !category.value().enabled) {
                operationError = invalidMasterData(QStringLiteral("lines[%1].batchId").arg(i));
                return false;
            }
            QString modelCode;
            if (line.productModelId > 0) {
                const auto model = masterData_.productModelById(line.productModelId);
                if (!model.ok()) {
                    operationError = error(model.error().code, model.error().message,
                                           QStringLiteral("lines[%1].productModelId").arg(i));
                    return false;
                }
                if (!model.value().enabled || model.value().productTypeId != batch.value().productTypeId) {
                    operationError = error(QStringLiteral("invalid_master_data"),
                        QStringLiteral("型号已停用或不属于该批次的产品类型"),
                        QStringLiteral("lines[%1].productModelId").arg(i));
                    return false;
                }
                modelCode = model.value().modelCode;
            }
            confirmedLines.append({batch.value(), productType.value(), category.value(), modelCode});
            const qint64 currentRequired = requiredByBatch.value(line.batchId);
            if (line.quantity > std::numeric_limits<qint64>::max() - currentRequired) {
                operationError = error(QStringLiteral("quantity_overflow"),
                                       QStringLiteral("明细数量合计超出范围"),
                                       QStringLiteral("lines[%1].quantity").arg(i));
                return false;
            }
            requiredByBatch[line.batchId] = currentRequired + line.quantity;
            if (!firstLineByBatch.contains(line.batchId)) firstLineByBatch.insert(line.batchId, i);
        }
        for (auto it = requiredByBatch.cbegin(); it != requiredByBatch.cend(); ++it) {
            const auto available = inventory_.quantityResult(it.key(), document.sourceLocationId);
            if (!available.ok()) {
                operationError = available.error();
                return false;
            }
            if (available.value() < it.value()) {
                const auto batch = documents_.inboundById(it.key());
                if (!batch.ok()) { operationError = batch.error(); return false; }
                operationError = error(QStringLiteral("insufficient_stock"),
                    QStringLiteral("批次 %1 在来源库位 %2 库存不足：可用 %3，请求 %4")
                        .arg(batch.value().batchNo, source.value().name,
                             QString::number(available.value()), QString::number(it.value())),
                    QStringLiteral("lines[%1].quantity").arg(firstLineByBatch.value(it.key())));
                return false;
            }
            if (internalMovement) {
                const auto targetQuantity = inventory_.quantityResult(it.key(), document.destinationLocationId);
                if (!targetQuantity.ok()) {
                    operationError = targetQuantity.error();
                    return false;
                }
                if (targetQuantity.value() > std::numeric_limits<qint64>::max() - it.value()) {
                    operationError = error(QStringLiteral("quantity_overflow"),
                        QStringLiteral("接收后的库存数量超出范围"),
                        QStringLiteral("lines[%1].quantity").arg(firstLineByBatch.value(it.key())));
                    return false;
                }
            }
        }

        for (int i = 0; i < document.lines.size(); ++i) {
            const LogisticsLine &line = document.lines.at(i);
            const auto &lineData = confirmedLines.at(i);
            const auto snapshot = documents_.updateLogisticsLineSnapshots(
                line.id, lineData.batch.batchNo, lineData.category.name,
                lineData.productType.typeCode, lineData.modelCode);
            if (!snapshot.ok()) {
                operationError = snapshot.error();
                return false;
            }
            const auto entry = inventory_.insertEntry(
                line.batchId, document.sourceLocationId, QStringLiteral("Logistics"), id,
                line.id, -line.quantity, document.businessAt);
            if (!entry.ok()) {
                operationError = entry.error();
                return false;
            }
            if (internalMovement) {
                const auto incoming = inventory_.insertEntry(
                    line.batchId, document.destinationLocationId, QStringLiteral("Logistics"), id,
                    line.id, line.quantity, document.businessAt);
                if (!incoming.ok()) {
                    operationError = incoming.error();
                    return false;
                }
            }
        }
        const auto confirmed = documents_.confirmLogistics(
            id, source.value().name, destination.value().name, QDateTime::currentDateTimeUtc(),
            outboundConfirmedByName);
        if (!confirmed.ok()) {
            operationError = confirmed.error();
            return false;
        }
        return true;
    }, &transactionError);
    if (!committed) {
        if (operationError.code.isEmpty())
            operationError = error(QStringLiteral("database"), transactionError);
        return Result<bool>::failure(operationError);
    }
    return Result<bool>::success(true);
}

Result<QVector<LogisticsDocument>> LogisticsService::list(DocumentType type) const
{
    return documents_.logisticsList(type);
}

Result<QVector<ReturnableLine>> LogisticsService::returnableLines(EntityId factoryLocationId) const
{
    const auto factory = masterData_.locationById(factoryLocationId);
    if (!factory.ok())
        return Result<QVector<ReturnableLine>>::failure(factory.error());
    if (!factory.value().enabled || factory.value().type != LocationType::Factory ||
        !factory.value().countsAsInventory)
        return Result<QVector<ReturnableLine>>::failure(error(QStringLiteral("invalid_master_data"),
            QStringLiteral("退回来源必须是已启用且计库存的工厂"), QStringLiteral("sourceLocationId")));
    return documents_.returnableLines(factoryLocationId);
}

Result<bool> LogisticsService::deleteDraft(EntityId id)
{
    return documents_.deleteLogisticsDraft(id);
}

Result<EntityId> LogisticsService::copyToDraft(EntityId id)
{
    const auto source = documents_.logisticsById(id);
    if (!source.ok())
        return Result<EntityId>::failure(source.error());
    if (source.value().status != DocumentStatus::Cancelled)
        return Result<EntityId>::failure(invalidStatus());
    LogisticsDraft copy;
    copy.type = source.value().type;
    copy.sourceLocationId = source.value().sourceLocationId;
    copy.destinationLocationId = source.value().destinationLocationId;
    copy.businessAt = source.value().businessAt;
    copy.note = source.value().note;
    for (const LogisticsLine &line : source.value().lines)
        copy.lines.append({line.batchId, line.quantity, line.productModelId});
    const ServiceError validation = validateDraft(copy, false);
    if (!validation.code.isEmpty())
        return Result<EntityId>::failure(validation);
    return documents_.insertLogisticsDraft(copy);
}
