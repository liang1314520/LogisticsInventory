#include "document_repository.h"

#include <QMetaType>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {
ServiceError databaseError(const QSqlQuery &query)
{
    return {QStringLiteral("database"), query.lastError().text(), {}};
}

ServiceError invalidStatus()
{
    return {QStringLiteral("invalid_status"), QStringLiteral("单据状态不允许此操作"),
            QStringLiteral("status")};
}

DocumentStatus statusFromText(const QString &text)
{
    if (text == QStringLiteral("Confirmed"))
        return DocumentStatus::Confirmed;
    if (text == QStringLiteral("Cancelled"))
        return DocumentStatus::Cancelled;
    return DocumentStatus::Draft;
}

QString typeText(DocumentType type)
{
    return type == DocumentType::Transfer ? QStringLiteral("Transfer")
                                          : QStringLiteral("Outbound");
}

QVariant nullableDocumentNumber(const QString &documentNo)
{
    return documentNo.isEmpty() ? QVariant(QMetaType::fromType<QString>())
                                : QVariant(documentNo);
}

QVariant nullableBatchNumber(const QString &batchNo)
{
    return batchNo.isEmpty() ? QVariant(QMetaType::fromType<QString>())
                             : QVariant(batchNo);
}

DocumentType typeFromText(const QString &text)
{
    return text == QStringLiteral("Outbound") ? DocumentType::Outbound
                                               : DocumentType::Transfer;
}

InboundBatch readBatch(const QSqlQuery &query)
{
    InboundBatch batch;
    batch.id = query.value(0).toLongLong();
    batch.batchNo = query.value(1).toString();
    batch.productTypeId = query.value(2).toLongLong();
    batch.quantity = query.value(3).toLongLong();
    batch.initialLocationId = query.value(4).toLongLong();
    batch.businessAt = QDateTime::fromString(query.value(5).toString(), Qt::ISODate);
    batch.status = statusFromText(query.value(6).toString());
    batch.note = query.value(7).toString();
    batch.categoryNameSnapshot = query.value(8).toString();
    batch.typeCodeSnapshot = query.value(9).toString();
    batch.locationNameSnapshot = query.value(10).toString();
    batch.createdAt = QDateTime::fromString(query.value(11).toString(), Qt::ISODate);
    batch.confirmedAt = QDateTime::fromString(query.value(12).toString(), Qt::ISODate);
    batch.cancelledAt = QDateTime::fromString(query.value(13).toString(), Qt::ISODate);
    return batch;
}

LogisticsDocument readLogistics(const QSqlQuery &query)
{
    LogisticsDocument document;
    document.id = query.value(0).toLongLong();
    document.documentNo = query.value(1).toString();
    document.type = typeFromText(query.value(2).toString());
    document.sourceLocationId = query.value(3).toLongLong();
    document.destinationLocationId = query.value(4).toLongLong();
    document.businessAt = QDateTime::fromString(query.value(5).toString(), Qt::ISODate);
    document.status = statusFromText(query.value(6).toString());
    document.note = query.value(7).toString();
    document.sourceNameSnapshot = query.value(8).toString();
    document.destinationNameSnapshot = query.value(9).toString();
    document.createdAt = QDateTime::fromString(query.value(10).toString(), Qt::ISODate);
    document.confirmedAt = QDateTime::fromString(query.value(11).toString(), Qt::ISODate);
    document.cancelledAt = QDateTime::fromString(query.value(12).toString(), Qt::ISODate);
    return document;
}

LogisticsLine readLogisticsLine(const QSqlQuery &query)
{
    LogisticsLine line;
    line.id = query.value(0).toLongLong();
    line.batchId = query.value(1).toLongLong();
    line.quantity = query.value(2).toLongLong();
    line.productTypeId = query.value(3).toLongLong();
    line.batchNoSnapshot = query.value(4).toString();
    line.categoryNameSnapshot = query.value(5).toString();
    line.typeCodeSnapshot = query.value(6).toString();
    return line;
}
}

DocumentRepository::DocumentRepository(Database &database) : database_(database) {}

Result<EntityId> DocumentRepository::insertInboundDraft(const InboundDraft &draft)
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "INSERT INTO inbound_batches(batch_no,product_type_id,quantity,initial_location_id,"
        "business_at,note,created_at) VALUES(?,?,?,?,?,?,?)"));
    query.addBindValue(nullableBatchNumber(draft.batchNo));
    query.addBindValue(draft.productTypeId);
    query.addBindValue(draft.quantity);
    query.addBindValue(draft.initialLocationId);
    query.addBindValue(draft.businessAt.toString(Qt::ISODate));
    query.addBindValue(draft.note.isNull() ? QStringLiteral("") : draft.note);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!query.exec())
        return Result<EntityId>::failure(databaseError(query));
    return Result<EntityId>::success(query.lastInsertId().toLongLong());
}

Result<bool> DocumentRepository::updateInboundDraft(EntityId id, const InboundDraft &draft)
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "UPDATE inbound_batches SET batch_no=?,product_type_id=?,quantity=?,"
        "initial_location_id=?,business_at=?,note=? WHERE id=? AND status='Draft'"));
    query.addBindValue(nullableBatchNumber(draft.batchNo));
    query.addBindValue(draft.productTypeId);
    query.addBindValue(draft.quantity);
    query.addBindValue(draft.initialLocationId);
    query.addBindValue(draft.businessAt.toString(Qt::ISODate));
    query.addBindValue(draft.note.isNull() ? QStringLiteral("") : draft.note);
    query.addBindValue(id);
    if (!query.exec())
        return Result<bool>::failure(databaseError(query));
    if (query.numRowsAffected() != 1)
        return Result<bool>::failure(invalidStatus());
    return Result<bool>::success(true);
}

Result<InboundBatch> DocumentRepository::inboundById(EntityId id) const
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "SELECT id,batch_no,product_type_id,quantity,initial_location_id,business_at,status,"
        "note,category_name_snapshot,type_code_snapshot,location_name_snapshot,"
        "created_at,confirmed_at,cancelled_at FROM inbound_batches WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec())
        return Result<InboundBatch>::failure(databaseError(query));
    if (!query.next())
        return Result<InboundBatch>::failure(
            {QStringLiteral("not_found"), QStringLiteral("入库批次不存在"), QStringLiteral("id")});
    return Result<InboundBatch>::success(readBatch(query));
}

Result<QVector<InboundBatch>> DocumentRepository::inboundList() const
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "SELECT id,batch_no,product_type_id,quantity,initial_location_id,business_at,status,"
        "note,category_name_snapshot,type_code_snapshot,location_name_snapshot,"
        "created_at,confirmed_at,cancelled_at FROM inbound_batches "
        "ORDER BY business_at DESC,created_at DESC,id DESC"));
    if (!query.exec()) return Result<QVector<InboundBatch>>::failure(databaseError(query));
    QVector<InboundBatch> rows;
    while (query.next()) rows.append(readBatch(query));
    if (query.lastError().isValid()) return Result<QVector<InboundBatch>>::failure(databaseError(query));
    return Result<QVector<InboundBatch>>::success(rows);
}

Result<bool> DocumentRepository::deleteInboundDraft(EntityId id)
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral("DELETE FROM inbound_batches WHERE id=? AND status='Draft'"));
    query.addBindValue(id);
    if (!query.exec()) return Result<bool>::failure(databaseError(query));
    if (query.numRowsAffected() != 1) return Result<bool>::failure(invalidStatus());
    return Result<bool>::success(true);
}

Result<bool> DocumentRepository::confirmInbound(
    EntityId id, const QString &categoryName, const QString &typeCode,
    const QString &locationName, const QDateTime &confirmedAt)
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "UPDATE inbound_batches SET status='Confirmed',category_name_snapshot=?,"
        "type_code_snapshot=?,location_name_snapshot=?,confirmed_at=? "
        "WHERE id=? AND status='Draft'"));
    query.addBindValue(categoryName);
    query.addBindValue(typeCode);
    query.addBindValue(locationName);
    query.addBindValue(confirmedAt.toString(Qt::ISODate));
    query.addBindValue(id);
    if (!query.exec())
        return Result<bool>::failure(databaseError(query));
    if (query.numRowsAffected() != 1)
        return Result<bool>::failure(invalidStatus());
    return Result<bool>::success(true);
}

Result<bool> DocumentRepository::cancelInbound(EntityId id, const QDateTime &cancelledAt)
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "UPDATE inbound_batches SET status='Cancelled',cancelled_at=? "
        "WHERE id=? AND status='Confirmed'"));
    query.addBindValue(cancelledAt.toString(Qt::ISODate));
    query.addBindValue(id);
    if (!query.exec())
        return Result<bool>::failure(databaseError(query));
    if (query.numRowsAffected() != 1)
        return Result<bool>::failure(invalidStatus());
    return Result<bool>::success(true);
}

Result<EntityId> DocumentRepository::insertLogisticsDraft(const LogisticsDraft &draft)
{
    ServiceError operationError;
    EntityId documentId = 0;
    QString transactionError;
    const bool committed = database_.transaction(Database::TransactionMode::Immediate, [&] {
        QSqlQuery header(database_.connection());
        header.prepare(QStringLiteral(
            "INSERT INTO logistics_documents(document_no,type,source_location_id,"
            "destination_location_id,business_at,note,created_at) VALUES(?,?,?,?,?,?,?)"));
        header.addBindValue(nullableDocumentNumber(draft.documentNo));
        header.addBindValue(typeText(draft.type));
        header.addBindValue(draft.sourceLocationId);
        header.addBindValue(draft.destinationLocationId);
        header.addBindValue(draft.businessAt.toString(Qt::ISODate));
        header.addBindValue(draft.note.isNull() ? QStringLiteral("") : draft.note);
        header.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        if (!header.exec()) {
            operationError = databaseError(header);
            return false;
        }
        documentId = header.lastInsertId().toLongLong();
        for (const LogisticsLineDraft &line : draft.lines) {
            QSqlQuery insert(database_.connection());
            insert.prepare(QStringLiteral(
                "INSERT INTO logistics_document_lines(document_id,batch_id,quantity) "
                "VALUES(?,?,?)"));
            insert.addBindValue(documentId);
            insert.addBindValue(line.batchId);
            insert.addBindValue(line.quantity);
            if (!insert.exec()) {
                operationError = databaseError(insert);
                return false;
            }
        }
        return true;
    }, &transactionError);
    if (!committed) {
        if (operationError.code.isEmpty())
            operationError = {QStringLiteral("database"), transactionError, {}};
        return Result<EntityId>::failure(operationError);
    }
    return Result<EntityId>::success(documentId);
}

Result<bool> DocumentRepository::updateLogisticsDraft(EntityId id,
                                                      const LogisticsDraft &draft)
{
    QSqlQuery header(database_.connection());
    header.prepare(QStringLiteral(
        "UPDATE logistics_documents SET document_no=?,type=?,source_location_id=?,"
        "destination_location_id=?,business_at=?,note=? WHERE id=? AND status='Draft'"));
    header.addBindValue(nullableDocumentNumber(draft.documentNo));
    header.addBindValue(typeText(draft.type));
    header.addBindValue(draft.sourceLocationId);
    header.addBindValue(draft.destinationLocationId);
    header.addBindValue(draft.businessAt.toString(Qt::ISODate));
    header.addBindValue(draft.note.isNull() ? QStringLiteral("") : draft.note);
    header.addBindValue(id);
    if (!header.exec())
        return Result<bool>::failure(databaseError(header));
    if (header.numRowsAffected() != 1)
        return Result<bool>::failure(invalidStatus());
    QSqlQuery remove(database_.connection());
    remove.prepare(QStringLiteral("DELETE FROM logistics_document_lines WHERE document_id=?"));
    remove.addBindValue(id);
    if (!remove.exec())
        return Result<bool>::failure(databaseError(remove));
    for (const LogisticsLineDraft &line : draft.lines) {
        QSqlQuery insert(database_.connection());
        insert.prepare(QStringLiteral(
            "INSERT INTO logistics_document_lines(document_id,batch_id,quantity) "
            "VALUES(?,?,?)"));
        insert.addBindValue(id);
        insert.addBindValue(line.batchId);
        insert.addBindValue(line.quantity);
        if (!insert.exec())
            return Result<bool>::failure(databaseError(insert));
    }
    return Result<bool>::success(true);
}

Result<LogisticsDocument> DocumentRepository::logisticsById(EntityId id) const
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "SELECT id,document_no,type,source_location_id,destination_location_id,business_at,"
        "status,note,source_name_snapshot,destination_name_snapshot,created_at,confirmed_at,"
        "cancelled_at FROM logistics_documents WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec())
        return Result<LogisticsDocument>::failure(databaseError(query));
    if (!query.next())
        return Result<LogisticsDocument>::failure(
            {QStringLiteral("not_found"), QStringLiteral("物流单据不存在"), QStringLiteral("id")});
    LogisticsDocument document = readLogistics(query);
    QSqlQuery lines(database_.connection());
    lines.prepare(QStringLiteral(
        "SELECT l.id,l.batch_id,l.quantity,b.product_type_id,"
        "l.batch_no_snapshot,l.category_name_snapshot,l.type_code_snapshot "
        "FROM logistics_document_lines l JOIN inbound_batches b ON b.id=l.batch_id "
        "WHERE l.document_id=? ORDER BY l.id"));
    lines.addBindValue(id);
    if (!lines.exec())
        return Result<LogisticsDocument>::failure(databaseError(lines));
    while (lines.next())
        document.lines.append(readLogisticsLine(lines));
    return Result<LogisticsDocument>::success(document);
}

Result<QVector<LogisticsDocument>> DocumentRepository::logisticsList(DocumentType type) const
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "SELECT id,document_no,type,source_location_id,destination_location_id,business_at,"
        "status,note,source_name_snapshot,destination_name_snapshot,created_at,confirmed_at,"
        "cancelled_at FROM logistics_documents WHERE type=? "
        "ORDER BY business_at DESC,created_at DESC,id DESC"));
    query.addBindValue(typeText(type));
    if (!query.exec()) return Result<QVector<LogisticsDocument>>::failure(databaseError(query));
    QVector<LogisticsDocument> rows;
    while (query.next()) rows.append(readLogistics(query));
    if (query.lastError().isValid()) return Result<QVector<LogisticsDocument>>::failure(databaseError(query));
    return Result<QVector<LogisticsDocument>>::success(rows);
}

Result<bool> DocumentRepository::deleteLogisticsDraft(EntityId id)
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral("DELETE FROM logistics_documents WHERE id=? AND status='Draft'"));
    query.addBindValue(id);
    if (!query.exec()) return Result<bool>::failure(databaseError(query));
    if (query.numRowsAffected() != 1) return Result<bool>::failure(invalidStatus());
    return Result<bool>::success(true);
}

Result<bool> DocumentRepository::updateLogisticsLineSnapshots(
    EntityId lineId, const QString &batchNo, const QString &categoryName,
    const QString &typeCode)
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "UPDATE logistics_document_lines SET batch_no_snapshot=?,category_name_snapshot=?,"
        "type_code_snapshot=? WHERE id=?"));
    query.addBindValue(batchNo);
    query.addBindValue(categoryName);
    query.addBindValue(typeCode);
    query.addBindValue(lineId);
    if (!query.exec())
        return Result<bool>::failure(databaseError(query));
    if (query.numRowsAffected() != 1)
        return Result<bool>::failure(
            {QStringLiteral("not_found"), QStringLiteral("物流明细不存在"), QStringLiteral("lineId")});
    return Result<bool>::success(true);
}

Result<bool> DocumentRepository::confirmLogistics(
    EntityId id, const QString &sourceName, const QString &destinationName,
    const QDateTime &confirmedAt)
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "UPDATE logistics_documents SET status='Confirmed',source_name_snapshot=?,"
        "destination_name_snapshot=?,confirmed_at=? WHERE id=? AND status='Draft'"));
    query.addBindValue(sourceName);
    query.addBindValue(destinationName);
    query.addBindValue(confirmedAt.toString(Qt::ISODate));
    query.addBindValue(id);
    if (!query.exec())
        return Result<bool>::failure(databaseError(query));
    if (query.numRowsAffected() != 1)
        return Result<bool>::failure(invalidStatus());
    return Result<bool>::success(true);
}

Result<bool> DocumentRepository::cancelLogistics(EntityId id,
                                                 const QDateTime &cancelledAt)
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "UPDATE logistics_documents SET status='Cancelled',cancelled_at=? "
        "WHERE id=? AND status='Confirmed'"));
    query.addBindValue(cancelledAt.toString(Qt::ISODate));
    query.addBindValue(id);
    if (!query.exec())
        return Result<bool>::failure(databaseError(query));
    if (query.numRowsAffected() != 1)
        return Result<bool>::failure(invalidStatus());
    return Result<bool>::success(true);
}
