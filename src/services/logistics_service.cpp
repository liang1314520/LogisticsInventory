#include "logistics_service.h"

#include <QHash>
#include <QSet>

#include <limits>

namespace {
ServiceError error(const QString &code, const QString &message, const QString &field = {})
{
    return {code, message, field};
}
}

LogisticsService::LogisticsService(Database &database, MasterDataRepository &masterData,
                                   DocumentRepository &documents, InventoryRepository &inventory)
    : database_(database), masterData_(masterData), documents_(documents), inventory_(inventory)
{
}

Result<LogisticsDocument> LogisticsService::prepare(const LogisticsInput &input,
                                                    const LogisticsDocument *current) const
{
    const auto fail = [](const QString &message, const QString &field) {
        return Result<LogisticsDocument>::failure(error(QStringLiteral("invalid"), message, field));
    };
    if (input.type != DocumentType::Transfer && input.type != DocumentType::Outbound &&
        input.type != DocumentType::Return)
        return fail(QStringLiteral("单据类型无效"), QStringLiteral("type"));
    if (current && current->type != input.type)
        return fail(QStringLiteral("单据类型不能修改，请作废后在对应页面新建"), QStringLiteral("type"));
    if (!input.businessAt.isValid())
        return fail(QStringLiteral("请选择有效的业务时间"), QStringLiteral("businessAt"));
    if (input.sourceLocationId <= 0)
        return fail(QStringLiteral("请选择来源库位"), QStringLiteral("sourceLocationId"));
    if (input.destinationLocationId <= 0 || input.sourceLocationId == input.destinationLocationId)
        return fail(QStringLiteral("请选择不同于来源的目的地"), QStringLiteral("destinationLocationId"));
    if (input.lines.isEmpty())
        return fail(QStringLiteral("至少需要一条明细"), QStringLiteral("lines"));
    const auto source = masterData_.locationById(input.sourceLocationId);
    if (!source.ok())
        return Result<LogisticsDocument>::failure(error(source.error().code, source.error().message,
                                                         QStringLiteral("sourceLocationId")));
    const auto destination = masterData_.locationById(input.destinationLocationId);
    if (!destination.ok())
        return Result<LogisticsDocument>::failure(error(destination.error().code, destination.error().message,
                                                         QStringLiteral("destinationLocationId")));
    if (!source.value().enabled && (!current || current->sourceLocationId != input.sourceLocationId))
        return fail(QStringLiteral("来源库位已停用"), QStringLiteral("sourceLocationId"));
    if (!destination.value().enabled &&
        (!current || current->destinationLocationId != input.destinationLocationId))
        return fail(QStringLiteral("目的地已停用"), QStringLiteral("destinationLocationId"));
    const bool sameRoute = current && current->sourceLocationId == input.sourceLocationId &&
        current->destinationLocationId == input.destinationLocationId;
    if (!sameRoute) {
        const auto sourceType = input.type == DocumentType::Transfer
            ? LocationType::Company : LocationType::Factory;
        if (source.value().type != sourceType || !source.value().countsAsInventory)
            return fail(input.type == DocumentType::Transfer
                ? QStringLiteral("调拨来源必须是计库存的公司库位")
                : QStringLiteral("出库或退回来源必须是计库存的工厂"), QStringLiteral("sourceLocationId"));
        const bool validDestination = input.type == DocumentType::Outbound
            ? !destination.value().countsAsInventory && destination.value().type != LocationType::Company &&
                destination.value().type != LocationType::Factory
            : destination.value().countsAsInventory && destination.value().type ==
                (input.type == DocumentType::Transfer ? LocationType::Factory : LocationType::Company);
        if (!validDestination)
            return fail(input.type == DocumentType::Transfer
                ? QStringLiteral("调拨目的地必须是计库存的工厂")
                : input.type == DocumentType::Return
                    ? QStringLiteral("退回目的地必须是计库存的公司库位")
                    : QStringLiteral("出库目的地必须是不计库存的外部地点（如海外 Amazon 仓）"),
                QStringLiteral("destinationLocationId"));
    }
    LogisticsDocument document;
    document.id = current ? current->id : 0;
    document.documentNo = input.documentNo.trimmed();
    document.type = input.type;
    document.sourceLocationId = input.sourceLocationId;
    document.destinationLocationId = input.destinationLocationId;
    document.businessAt = input.businessAt;
    document.note = input.note.trimmed();
    document.sourceNameSnapshot = source.value().name;
    document.destinationNameSnapshot = destination.value().name;
    const auto now = QDateTime::currentDateTimeUtc();
    document.createdAt = current ? current->createdAt : now;
    document.confirmedAt = current && current->status == DocumentStatus::Confirmed
        ? current->confirmedAt : now;
    if (input.type == DocumentType::Outbound) {
        if (input.outboundConfirmedById <= 0)
            return Result<LogisticsDocument>::failure(error(QStringLiteral("required"),
                QStringLiteral("请选择出库确认人"), QStringLiteral("outboundConfirmedById")));
        const auto person = masterData_.personnelById(input.outboundConfirmedById);
        if (!person.ok())
            return Result<LogisticsDocument>::failure(error(person.error().code, person.error().message,
                                                             QStringLiteral("outboundConfirmedById")));
        if (!person.value().enabled &&
            (!current || current->outboundConfirmedById != input.outboundConfirmedById))
            return fail(QStringLiteral("出库确认人已停用，请重新选择"), QStringLiteral("outboundConfirmedById"));
        document.outboundConfirmedById = person.value().id;
        document.outboundConfirmedByName = person.value().name;
    }
    QSet<EntityId> originalBatches;
    QSet<QPair<EntityId, EntityId>> originalChoices;
    if (current) {
        for (const auto &line : current->lines) {
            originalBatches.insert(line.batchId);
            originalChoices.insert(qMakePair(line.batchId, line.productModelId));
        }
    }
    QSet<QPair<EntityId, EntityId>> choices;
    QHash<EntityId, qint64> totals;
    for (int i = 0; i < input.lines.size(); ++i) {
        const auto &value = input.lines.at(i);
        const QString prefix = QStringLiteral("lines[%1].").arg(i);
        if (value.batchId <= 0)
            return fail(QStringLiteral("明细批次无效"), prefix + QStringLiteral("batchId"));
        if (value.productModelId < 0)
            return fail(QStringLiteral("型号无效"), prefix + QStringLiteral("productModelId"));
        if (value.quantity <= 0)
            return fail(QStringLiteral("明细数量必须为正整数"), prefix + QStringLiteral("quantity"));
        const auto choice = qMakePair(value.batchId, value.productModelId);
        if (choices.contains(choice))
            return Result<LogisticsDocument>::failure(error(QStringLiteral("duplicate_batch"),
                QStringLiteral("同一批次的同一型号请合并为一行"), prefix + QStringLiteral("batchId")));
        choices.insert(choice);
        if (value.quantity > std::numeric_limits<qint64>::max() - totals.value(value.batchId))
            return Result<LogisticsDocument>::failure(error(QStringLiteral("quantity_overflow"),
                QStringLiteral("同批次数量合计超出范围"), prefix + QStringLiteral("quantity")));
        totals[value.batchId] += value.quantity;
        const auto batch = documents_.inboundById(value.batchId);
        if (!batch.ok())
            return Result<LogisticsDocument>::failure(error(batch.error().code, batch.error().message,
                                                             prefix + QStringLiteral("batchId")));
        const auto product = masterData_.productTypeById(batch.value().productTypeId);
        if (!product.ok())
            return Result<LogisticsDocument>::failure(error(product.error().code, product.error().message,
                                                             prefix + QStringLiteral("batchId")));
        const auto category = masterData_.categoryById(product.value().categoryId);
        if (!category.ok())
            return Result<LogisticsDocument>::failure(error(category.error().code, category.error().message,
                                                             prefix + QStringLiteral("batchId")));
        if (!originalBatches.contains(value.batchId) &&
            (batch.value().status != DocumentStatus::Confirmed ||
             !product.value().enabled || !category.value().enabled))
            return fail(QStringLiteral("批次已作废或产品资料已停用"), prefix + QStringLiteral("batchId"));
        LogisticsLine line;
        static_cast<LogisticsLineInput &>(line) = value;
        line.productTypeId = product.value().id;
        line.batchNoSnapshot = batch.value().batchNo;
        line.categoryNameSnapshot = category.value().name;
        line.typeCodeSnapshot = product.value().typeCode;
        line.materialModel = batch.value().materialModel;
        if (value.productModelId > 0) {
            const auto model = masterData_.productModelById(value.productModelId);
            if (!model.ok())
                return Result<LogisticsDocument>::failure(error(model.error().code, model.error().message,
                                                                 prefix + QStringLiteral("productModelId")));
            if (!originalChoices.contains(choice) &&
                (!model.value().enabled || model.value().productTypeId != product.value().id))
                return fail(QStringLiteral("型号已停用或不属于该批次的产品"),
                            prefix + QStringLiteral("productModelId"));
            line.modelCodeSnapshot = model.value().modelCode;
        }
        document.lines.append(line);
    }
    return Result<LogisticsDocument>::success(document);
}

Result<bool> LogisticsService::writeEntries(const LogisticsDocument &document)
{
    for (const auto &line : document.lines) {
        const auto outgoing = inventory_.insertEntry(line.batchId, document.sourceLocationId,
            QStringLiteral("Logistics"), document.id, line.id, -line.quantity, document.businessAt);
        if (!outgoing.ok())
            return Result<bool>::failure(outgoing.error());
        if (document.type != DocumentType::Outbound) {
            const auto incoming = inventory_.insertEntry(line.batchId, document.destinationLocationId,
                QStringLiteral("Logistics"), document.id, line.id, line.quantity, document.businessAt);
            if (!incoming.ok())
                return Result<bool>::failure(incoming.error());
        }
    }
    return Result<bool>::success(true);
}

Result<EntityId> LogisticsService::save(EntityId id, const LogisticsInput &input)
{
    ServiceError operationError;
    QString transactionError;
    const bool committed = database_.transaction(Database::TransactionMode::Immediate, [&] {
        LogisticsDocument current;
        if (id) {
            const auto original = documents_.logisticsById(id);
            if (!original.ok()) { operationError = original.error(); return false; }
            current = original.value();
        }
        const auto prepared = prepare(input, id ? &current : nullptr);
        if (!prepared.ok()) { operationError = prepared.error(); return false; }
        if (id) {
            const auto removed = inventory_.removeEntriesForSource(QStringLiteral("Logistics"), id);
            if (!removed.ok()) { operationError = removed.error(); return false; }
            const auto updated = documents_.updateLogistics(prepared.value());
            if (!updated.ok()) { operationError = updated.error(); return false; }
        } else {
            const auto inserted = documents_.insertLogistics(prepared.value());
            if (!inserted.ok()) { operationError = inserted.error(); return false; }
            id = inserted.value();
        }
        const auto saved = documents_.logisticsById(id);
        if (!saved.ok()) { operationError = saved.error(); return false; }
        const auto entries = writeEntries(saved.value());
        if (!entries.ok()) { operationError = entries.error(); return false; }
        return true;
    }, &transactionError);
    if (!committed)
        return Result<EntityId>::failure(operationError.code.isEmpty()
            ? error(QStringLiteral("database"), transactionError) : operationError);
    return Result<EntityId>::success(id);
}

Result<EntityId> LogisticsService::create(const LogisticsInput &input)
{
    return save(0, input);
}

Result<bool> LogisticsService::update(EntityId id, const LogisticsInput &input)
{
    if (id <= 0)
        return Result<bool>::failure(error(QStringLiteral("invalid"), QStringLiteral("单据ID无效"),
                                           QStringLiteral("id")));
    const auto saved = save(id, input);
    return saved.ok() ? Result<bool>::success(true) : Result<bool>::failure(saved.error());
}

Result<LogisticsDocument> LogisticsService::byId(EntityId id) const
{
    return documents_.logisticsById(id);
}

Result<QVector<LogisticsDocument>> LogisticsService::list(DocumentType type) const
{
    return documents_.logisticsList(type);
}

Result<bool> LogisticsService::cancel(EntityId id)
{
    ServiceError operationError;
    QString transactionError;
    const bool committed = database_.transaction(Database::TransactionMode::Immediate, [&] {
        const auto document = documents_.logisticsById(id);
        if (!document.ok()) { operationError = document.error(); return false; }
        if (document.value().status == DocumentStatus::Cancelled)
            return true;
        const auto originals = inventory_.unreversedEntriesForSource(QStringLiteral("Logistics"), id);
        if (!originals.ok()) { operationError = originals.error(); return false; }
        for (const auto &entry : originals.value()) {
            const auto reversal = inventory_.insertEntry(entry.batchId, entry.locationId,
                QStringLiteral("Logistics"), id, entry.sourceLineId, -entry.quantityDelta,
                document.value().businessAt, entry.id);
            if (!reversal.ok()) { operationError = reversal.error(); return false; }
        }
        const auto cancelled = documents_.cancelLogistics(id, QDateTime::currentDateTimeUtc());
        if (!cancelled.ok()) { operationError = cancelled.error(); return false; }
        return true;
    }, &transactionError);
    if (!committed)
        return Result<bool>::failure(operationError.code.isEmpty()
            ? error(QStringLiteral("database"), transactionError) : operationError);
    return Result<bool>::success(true);
}
