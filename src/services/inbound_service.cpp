#include "inbound_service.h"

namespace {
ServiceError invalid(const QString &field)
{
    return {QStringLiteral("invalid"), QStringLiteral("字段值无效"), field};
}
}

InboundService::InboundService(Database &database, MasterDataRepository &masterData,
                               DocumentRepository &documents, InventoryRepository &inventory)
    : database_(database), masterData_(masterData), documents_(documents), inventory_(inventory)
{
}

Result<InboundBatch> InboundService::prepare(const InboundInput &input,
                                             const InboundBatch *current) const
{
    if (input.quantity <= 0)
        return Result<InboundBatch>::failure(invalid(QStringLiteral("quantity")));
    if (!input.businessAt.isValid())
        return Result<InboundBatch>::failure(invalid(QStringLiteral("businessAt")));
    if (input.materialModel.trimmed().isEmpty())
        return Result<InboundBatch>::failure({QStringLiteral("required"),
            QStringLiteral("请填写物料型号（例如 V40）"), QStringLiteral("materialModel")});
    if (input.receivedById <= 0)
        return Result<InboundBatch>::failure({QStringLiteral("required"),
            QStringLiteral("请选择入库签收人"), QStringLiteral("receivedById")});
    const auto person = masterData_.personnelById(input.receivedById);
    if (!person.ok())
        return Result<InboundBatch>::failure({person.error().code, person.error().message,
                                               QStringLiteral("receivedById")});
    if (!person.value().enabled && (!current || current->receivedById != input.receivedById))
        return Result<InboundBatch>::failure({QStringLiteral("invalid_master_data"),
            QStringLiteral("入库签收人已停用，请重新选择"), QStringLiteral("receivedById")});
    const auto product = masterData_.productTypeById(input.productTypeId);
    if (!product.ok())
        return Result<InboundBatch>::failure({product.error().code, product.error().message,
                                               QStringLiteral("productTypeId")});
    const auto category = masterData_.categoryById(product.value().categoryId);
    if (!category.ok())
        return Result<InboundBatch>::failure({category.error().code, category.error().message,
                                               QStringLiteral("productTypeId")});
    if ((!product.value().enabled || !category.value().enabled) &&
        (!current || current->productTypeId != input.productTypeId)) {
        return Result<InboundBatch>::failure({QStringLiteral("invalid_master_data"),
            QStringLiteral("产品或分类已停用，请重新选择"), QStringLiteral("productTypeId")});
    }
    const auto location = masterData_.locationById(input.initialLocationId);
    if (!location.ok())
        return Result<InboundBatch>::failure({location.error().code, location.error().message,
                                               QStringLiteral("initialLocationId")});
    if ((!location.value().enabled || location.value().type != LocationType::Company ||
         !location.value().countsAsInventory) &&
        (!current || current->initialLocationId != input.initialLocationId)) {
        return Result<InboundBatch>::failure({QStringLiteral("invalid_master_data"),
            QStringLiteral("新物料必须先入库到已启用且计库存的公司库位"),
            QStringLiteral("initialLocationId")});
    }
    InboundBatch batch;
    static_cast<InboundInput &>(batch) = input;
    batch.batchNo = input.batchNo.trimmed();
    batch.note = input.note.trimmed();
    batch.materialModel = input.materialModel.trimmed();
    batch.id = current ? current->id : 0;
    const auto now = QDateTime::currentDateTimeUtc();
    batch.createdAt = current ? current->createdAt : now;
    batch.confirmedAt = current && current->status == DocumentStatus::Confirmed
        ? current->confirmedAt : now;
    batch.categoryNameSnapshot = category.value().name;
    batch.typeCodeSnapshot = product.value().typeCode;
    batch.locationNameSnapshot = location.value().name;
    batch.receivedByName = person.value().name;
    return Result<InboundBatch>::success(batch);
}

Result<EntityId> InboundService::save(EntityId id, const InboundInput &input)
{
    ServiceError operationError;
    QString transactionError;
    const bool committed = database_.transaction(Database::TransactionMode::Immediate, [&] {
        InboundBatch current;
        if (id) {
            const auto original = documents_.inboundById(id);
            if (!original.ok()) { operationError = original.error(); return false; }
            current = original.value();
        }
        const auto prepared = prepare(input, id ? &current : nullptr);
        if (!prepared.ok()) { operationError = prepared.error(); return false; }
        if (id) {
            const auto removed = inventory_.removeEntriesForSource(QStringLiteral("Inbound"), id);
            if (!removed.ok()) { operationError = removed.error(); return false; }
            const auto updated = documents_.updateInbound(prepared.value());
            if (!updated.ok()) { operationError = updated.error(); return false; }
            const auto snapshots = documents_.updateBatchSnapshots(id);
            if (!snapshots.ok()) { operationError = snapshots.error(); return false; }
        } else {
            const auto inserted = documents_.insertInbound(prepared.value());
            if (!inserted.ok()) { operationError = inserted.error(); return false; }
            id = inserted.value();
        }
        const auto &batch = prepared.value();
        const auto entry = inventory_.insertEntry(id, batch.initialLocationId,
            QStringLiteral("Inbound"), id, 0, batch.quantity, batch.businessAt);
        if (!entry.ok()) { operationError = entry.error(); return false; }
        return true;
    }, &transactionError);
    if (!committed)
        return Result<EntityId>::failure(operationError.code.isEmpty()
            ? ServiceError{QStringLiteral("database"), transactionError, {}} : operationError);
    return Result<EntityId>::success(id);
}

Result<EntityId> InboundService::create(const InboundInput &input)
{
    return save(0, input);
}

Result<bool> InboundService::update(EntityId id, const InboundInput &input)
{
    if (id <= 0)
        return Result<bool>::failure(invalid(QStringLiteral("id")));
    const auto saved = save(id, input);
    return saved.ok() ? Result<bool>::success(true) : Result<bool>::failure(saved.error());
}

Result<InboundBatch> InboundService::byId(EntityId id) const
{
    return documents_.inboundById(id);
}

Result<QVector<InboundBatch>> InboundService::list() const
{
    return documents_.inboundList();
}

Result<bool> InboundService::cancel(EntityId id)
{
    ServiceError operationError;
    QString transactionError;
    const bool committed = database_.transaction(Database::TransactionMode::Immediate, [&] {
        const auto batch = documents_.inboundById(id);
        if (!batch.ok()) { operationError = batch.error(); return false; }
        if (batch.value().status == DocumentStatus::Cancelled)
            return true;
        const auto originals = inventory_.unreversedEntriesForSource(QStringLiteral("Inbound"), id);
        if (!originals.ok()) { operationError = originals.error(); return false; }
        for (const auto &entry : originals.value()) {
            const auto reversal = inventory_.insertEntry(entry.batchId, entry.locationId,
                QStringLiteral("Inbound"), id, entry.sourceLineId, -entry.quantityDelta,
                batch.value().businessAt, entry.id);
            if (!reversal.ok()) { operationError = reversal.error(); return false; }
        }
        const auto cancelled = documents_.cancelInbound(id, QDateTime::currentDateTimeUtc());
        if (!cancelled.ok()) { operationError = cancelled.error(); return false; }
        return true;
    }, &transactionError);
    if (!committed)
        return Result<bool>::failure(operationError.code.isEmpty()
            ? ServiceError{QStringLiteral("database"), transactionError, {}} : operationError);
    return Result<bool>::success(true);
}
