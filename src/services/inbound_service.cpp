#include "inbound_service.h"

#include <QHash>

namespace {
ServiceError required(const QString &field)
{
    return {QStringLiteral("required"), QStringLiteral("必填字段不能为空"), field};
}

ServiceError invalid(const QString &field)
{
    return {QStringLiteral("invalid"), QStringLiteral("字段值无效"), field};
}

ServiceError invalidStatus()
{
    return {QStringLiteral("invalid_status"), QStringLiteral("单据状态不允许此操作"),
            QStringLiteral("status")};
}

ServiceError invalidMasterData(const QString &field)
{
    return {QStringLiteral("invalid_master_data"), QStringLiteral("主数据不可用于确认"), field};
}

ServiceError stockDependency()
{
    return {QStringLiteral("stock_dependency"),
            QStringLiteral("取消将导致库存为负，存在后续库存依赖"), {}};
}
}

InboundService::InboundService(Database &database, MasterDataRepository &masterData,
                               DocumentRepository &documents,
                               InventoryRepository &inventory)
    : database_(database), masterData_(masterData), documents_(documents), inventory_(inventory)
{
}

ServiceError InboundService::validateDraft(const InboundDraft &draft, bool requireNumber) const
{
    if (requireNumber && draft.batchNo.trimmed().isEmpty())
        return required(QStringLiteral("batchNo"));
    if (draft.productTypeId <= 0)
        return invalid(QStringLiteral("productTypeId"));
    if (draft.materialModel.trimmed().isEmpty())
        return {QStringLiteral("required"), QStringLiteral("请填写物料型号（例如 V40）"),
                QStringLiteral("materialModel")};
    if (draft.quantity <= 0)
        return invalid(QStringLiteral("quantity"));
    if (draft.initialLocationId <= 0)
        return invalid(QStringLiteral("initialLocationId"));
    if (!draft.businessAt.isValid())
        return invalid(QStringLiteral("businessAt"));
    if (draft.receivedById < 0)
        return invalid(QStringLiteral("receivedById"));
    if (draft.receivedById > 0) {
        const auto person = masterData_.personnelById(draft.receivedById);
        if (!person.ok())
            return {person.error().code, person.error().message, QStringLiteral("receivedById")};
        if (!person.value().enabled)
            return {QStringLiteral("invalid_master_data"),
                QStringLiteral("入库签收人已停用，请编辑草稿重新选择"), QStringLiteral("receivedById")};
    }
    const auto location = masterData_.locationById(draft.initialLocationId);
    if (!location.ok())
        return {location.error().code, location.error().message, QStringLiteral("initialLocationId")};
    if (!location.value().enabled || location.value().type != LocationType::Company ||
        !location.value().countsAsInventory)
        return {QStringLiteral("invalid_master_data"),
                QStringLiteral("新物料必须先入库到已启用且计库存的公司库位"),
                QStringLiteral("initialLocationId")};
    return {};
}

Result<EntityId> InboundService::createDraft(const InboundDraft &draft)
{
    const ServiceError error = validateDraft(draft, false);
    if (!error.code.isEmpty())
        return Result<EntityId>::failure(error);
    InboundDraft normalized = draft;
    normalized.batchNo = draft.batchNo.trimmed();
    normalized.note = draft.note.trimmed();
    normalized.materialModel = draft.materialModel.trimmed();
    return documents_.insertInboundDraft(normalized);
}

Result<bool> InboundService::updateDraft(EntityId id, const InboundDraft &draft)
{
    const ServiceError error = validateDraft(draft, false);
    if (!error.code.isEmpty())
        return Result<bool>::failure(error);
    InboundDraft normalized = draft;
    normalized.batchNo = draft.batchNo.trimmed();
    normalized.note = draft.note.trimmed();
    normalized.materialModel = draft.materialModel.trimmed();
    return documents_.updateInboundDraft(id, normalized);
}

Result<InboundBatch> InboundService::byId(EntityId id) const
{
    return documents_.inboundById(id);
}

Result<bool> InboundService::confirm(EntityId id)
{
    ServiceError operationError;
    QString transactionError;
    const bool committed = database_.transaction(Database::TransactionMode::Immediate, [&] {
        const auto batchResult = documents_.inboundById(id);
        if (!batchResult.ok()) {
            operationError = batchResult.error();
            return false;
        }
        const InboundBatch &batch = batchResult.value();
        if (batch.status != DocumentStatus::Draft) {
            operationError = invalidStatus();
            return false;
        }
        const ServiceError draftError = validateDraft(batch);
        if (!draftError.code.isEmpty()) {
            operationError = draftError;
            return false;
        }
        if (batch.receivedById == 0) {
            operationError = {QStringLiteral("required"),
                QStringLiteral("请先编辑草稿选择入库签收人，再确认入库"), QStringLiteral("receivedById")};
            return false;
        }
        const auto person = masterData_.personnelById(batch.receivedById);
        if (!person.ok()) {
            operationError = {person.error().code, person.error().message, QStringLiteral("receivedById")};
            return false;
        }
        const auto productType = masterData_.productTypeById(batch.productTypeId);
        if (!productType.ok() || !productType.value().enabled) {
            operationError = productType.ok() ? invalidMasterData(QStringLiteral("productTypeId"))
                                              : productType.error();
            return false;
        }
        const auto category = masterData_.categoryById(productType.value().categoryId);
        if (!category.ok() || !category.value().enabled) {
            operationError = category.ok() ? invalidMasterData(QStringLiteral("productTypeId"))
                                           : category.error();
            return false;
        }
        const auto location = masterData_.locationById(batch.initialLocationId);
        if (!location.ok() || !location.value().enabled || !location.value().countsAsInventory) {
            operationError = location.ok() ? invalidMasterData(QStringLiteral("initialLocationId"))
                                           : location.error();
            return false;
        }
        const auto entry = inventory_.insertEntry(batch.id, batch.initialLocationId,
                                                  QStringLiteral("Inbound"), batch.id, 0,
                                                  batch.quantity, batch.businessAt);
        if (!entry.ok()) {
            operationError = entry.error();
            return false;
        }
        const auto confirmed = documents_.confirmInbound(
            batch.id, category.value().name, productType.value().typeCode,
            location.value().name, QDateTime::currentDateTimeUtc(), person.value().name);
        if (!confirmed.ok()) {
            operationError = confirmed.error();
            return false;
        }
        return true;
    }, &transactionError);
    if (!committed) {
        if (operationError.code.isEmpty())
            operationError = {QStringLiteral("database"), transactionError, {}};
        return Result<bool>::failure(operationError);
    }
    return Result<bool>::success(true);
}

Result<QVector<InboundBatch>> InboundService::list() const
{
    return documents_.inboundList();
}

Result<bool> InboundService::deleteDraft(EntityId id)
{
    return documents_.deleteInboundDraft(id);
}

Result<EntityId> InboundService::copyToDraft(EntityId id)
{
    const auto source = documents_.inboundById(id);
    if (!source.ok()) return Result<EntityId>::failure(source.error());
    if (source.value().status != DocumentStatus::Cancelled)
        return Result<EntityId>::failure(invalidStatus());
    InboundDraft copy = source.value();
    copy.batchNo.clear();
    copy.receivedById = 0;
    if (copy.materialModel.trimmed().isEmpty())
        return Result<EntityId>::failure({QStringLiteral("required"),
            QStringLiteral("原批次缺少物料型号，请新建入库并填写"),
            QStringLiteral("materialModel")});
    return createDraft(copy);
}

Result<bool> InboundService::cancel(EntityId id)
{
    ServiceError operationError;
    QString transactionError;
    const bool committed = database_.transaction(Database::TransactionMode::Immediate, [&] {
        const auto batchResult = documents_.inboundById(id);
        if (!batchResult.ok()) {
            operationError = batchResult.error();
            return false;
        }
        const InboundBatch &batch = batchResult.value();
        if (batch.status != DocumentStatus::Confirmed) {
            operationError = invalidStatus();
            return false;
        }
        const auto originals = inventory_.unreversedEntriesForSource(
            QStringLiteral("Inbound"), id);
        if (!originals.ok()) {
            operationError = originals.error();
            return false;
        }
        if (originals.value().isEmpty()) {
            operationError = {QStringLiteral("invalid_state"),
                              QStringLiteral("未找到可冲销的入库流水"), {}};
            return false;
        }

        QHash<QString, qint64> resultingQuantities;
        for (const InventoryEntry &entry : originals.value()) {
            const QString key = QStringLiteral("%1:%2").arg(entry.batchId).arg(entry.locationId);
            if (!resultingQuantities.contains(key)) {
                const auto quantity = inventory_.quantityResult(entry.batchId, entry.locationId);
                if (!quantity.ok()) {
                    operationError = quantity.error();
                    return false;
                }
                resultingQuantities.insert(key, quantity.value());
            }
            resultingQuantities[key] -= entry.quantityDelta;
            if (resultingQuantities.value(key) < 0) {
                operationError = stockDependency();
                return false;
            }
        }
        for (const InventoryEntry &entry : originals.value()) {
            const auto reversal = inventory_.insertEntry(
                entry.batchId, entry.locationId, QStringLiteral("Inbound"), id,
                entry.sourceLineId, -entry.quantityDelta, batch.businessAt, entry.id);
            if (!reversal.ok()) {
                operationError = reversal.error();
                return false;
            }
        }
        const auto cancelled = documents_.cancelInbound(id, QDateTime::currentDateTimeUtc());
        if (!cancelled.ok()) {
            operationError = cancelled.error();
            return false;
        }
        return true;
    }, &transactionError);
    if (!committed) {
        if (operationError.code.isEmpty())
            operationError = {QStringLiteral("database"), transactionError, {}};
        return Result<bool>::failure(operationError);
    }
    return Result<bool>::success(true);
}
