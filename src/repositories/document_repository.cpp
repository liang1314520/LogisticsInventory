#include "document_repository.h"

#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>
#include <limits>

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
    return text == QStringLiteral("Cancelled") ? DocumentStatus::Cancelled
                                               : DocumentStatus::Confirmed;
}

QString typeText(DocumentType type)
{
    if (type == DocumentType::Return)
        return QStringLiteral("Return");
    return type == DocumentType::Transfer ? QStringLiteral("Transfer")
                                          : QStringLiteral("Outbound");
}

QString numberPrefix(DocumentType type)
{
    if (type == DocumentType::Return)
        return QStringLiteral("TH");
    return type == DocumentType::Transfer ? QStringLiteral("DB") : QStringLiteral("CK");
}

DocumentType typeFromText(const QString &text)
{
    if (text == QStringLiteral("Return"))
        return DocumentType::Return;
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
    batch.materialModel = query.value(14).toString();
    batch.receivedById = query.value(15).toLongLong();
    batch.receivedByName = query.value(16).toString();
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
    document.outboundConfirmedById = query.value(13).toLongLong();
    document.outboundConfirmedByName = query.value(14).toString();
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
    line.productModelId = query.value(7).toLongLong();
    line.modelCodeSnapshot = query.value(8).toString();
    line.materialModel = query.value(9).toString();
    return line;
}
}

DocumentRepository::DocumentRepository(Database &database) : database_(database) {}

Result<QString> DocumentRepository::prepareNumber(const QString &requested,
                                                   const QString &prefix,
                                                   const QDate &businessDate,
                                                   EntityId excludedId)
{
    const bool inbound = prefix == QStringLiteral("B");
    const QString table = inbound ? QStringLiteral("inbound_batches")
                                  : QStringLiteral("logistics_documents");
    const QString column = inbound ? QStringLiteral("batch_no") : QStringLiteral("document_no");
    const QString field = inbound ? QStringLiteral("batchNo") : QStringLiteral("documentNo");
    QString number = requested.trimmed();
    const auto saveSequence = [this](const QString &kind, const QString &date, qint64 value) {
        QSqlQuery query(database_.connection());
        query.prepare(QStringLiteral(
            "INSERT INTO document_number_sequences(prefix,business_date,last_value) VALUES(?,?,?) "
            "ON CONFLICT(prefix,business_date) DO UPDATE SET "
            "last_value=MAX(last_value,excluded.last_value)"));
        query.addBindValue(kind);
        query.addBindValue(date);
        query.addBindValue(value);
        if (!query.exec())
            return Result<bool>::failure(databaseError(query));
        return Result<bool>::success(true);
    };

    if (number.isEmpty()) {
        if (!businessDate.isValid())
            return Result<QString>::failure({QStringLiteral("invalid"),
                QStringLiteral("请选择有效的业务日期"), QStringLiteral("businessAt")});
        const QString date = businessDate.toString(QStringLiteral("yyyyMMdd"));
        const QString stem = prefix + QLatin1Char('-') + date + QLatin1Char('-');
        qint64 last = 0;
        QSqlQuery sequence(database_.connection());
        sequence.prepare(QStringLiteral(
            "SELECT last_value FROM document_number_sequences WHERE prefix=? AND business_date=?"));
        sequence.addBindValue(prefix);
        sequence.addBindValue(date);
        if (!sequence.exec())
            return Result<QString>::failure(databaseError(sequence));
        if (sequence.next())
            last = sequence.value(0).toLongLong();
        else if (sequence.lastError().isValid())
            return Result<QString>::failure(databaseError(sequence));
        sequence.finish();

        // 首次使用号段时也计入已有编号，避免与历史或手动编号重复。
        QSqlQuery existing(database_.connection());
        existing.prepare(QStringLiteral("SELECT %1 FROM %2 WHERE %1 LIKE ?").arg(column, table));
        existing.addBindValue(stem + QLatin1Char('%'));
        if (!existing.exec())
            return Result<QString>::failure(databaseError(existing));
        const QRegularExpression suffixPattern(QStringLiteral("^[0-9]{4,}$"));
        while (existing.next()) {
            const QString suffix = existing.value(0).toString().mid(stem.size());
            bool ok = false;
            const qint64 value = suffix.toLongLong(&ok);
            if (ok && suffixPattern.match(suffix).hasMatch())
                last = std::max(last, value);
        }
        if (existing.lastError().isValid())
            return Result<QString>::failure(databaseError(existing));
        existing.finish();
        if (last == std::numeric_limits<qint64>::max())
            return Result<QString>::failure({QStringLiteral("number_exhausted"),
                QStringLiteral("该日期的编号已超出可用范围，请使用手动编号"), field});
        number = stem + QStringLiteral("%1").arg(last + 1, 4, 10, QLatin1Char('0'));
        const auto saved = saveSequence(prefix, date, last + 1);
        if (!saved.ok())
            return Result<QString>::failure(saved.error());
    } else {
        QSqlQuery existing(database_.connection());
        existing.prepare(QStringLiteral("SELECT 1 FROM %1 WHERE %2=? AND id<>? LIMIT 1")
                             .arg(table, column));
        existing.addBindValue(number);
        existing.addBindValue(excludedId);
        if (!existing.exec())
            return Result<QString>::failure(databaseError(existing));
        if (existing.next())
            return Result<QString>::failure({QStringLiteral("duplicate"),
                inbound ? QStringLiteral("批次号已存在，请使用其他编号")
                        : QStringLiteral("单号已存在，请使用其他编号"), field});
        if (existing.lastError().isValid())
            return Result<QString>::failure(databaseError(existing));
        existing.finish();
        const QRegularExpression pattern(inbound
            ? QStringLiteral("^(B)-([0-9]{8})-([0-9]{4,})$")
            : QStringLiteral("^(DB|CK|TH)-([0-9]{8})-([0-9]{4,})$"));
        const auto match = pattern.match(number);
        if (match.hasMatch() && QDate::fromString(match.captured(2), QStringLiteral("yyyyMMdd")).isValid()) {
            bool ok = false;
            const qint64 value = match.captured(3).toLongLong(&ok);
            if (ok) {
                const auto saved = saveSequence(match.captured(1), match.captured(2), value);
                if (!saved.ok())
                    return Result<QString>::failure(saved.error());
            }
        }
    }
    return Result<QString>::success(number);
}

Result<EntityId> DocumentRepository::insertInbound(const InboundBatch &batch)
{
    const auto number = prepareNumber(batch.batchNo, QStringLiteral("B"), batch.businessAt.date());
    if (!number.ok())
        return Result<EntityId>::failure(number.error());
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "INSERT INTO inbound_batches(batch_no,product_type_id,quantity,initial_location_id,"
        "business_at,note,created_at,material_model,received_by_id,category_name_snapshot,"
        "type_code_snapshot,location_name_snapshot,confirmed_at,received_by_name_snapshot,status) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,'Confirmed')"));
    query.addBindValue(number.value());
    query.addBindValue(batch.productTypeId);
    query.addBindValue(batch.quantity);
    query.addBindValue(batch.initialLocationId);
    query.addBindValue(batch.businessAt.toString(Qt::ISODate));
    query.addBindValue(batch.note.isNull() ? QStringLiteral("") : batch.note);
    query.addBindValue(batch.createdAt.toString(Qt::ISODate));
    query.addBindValue(batch.materialModel);
    query.addBindValue(batch.receivedById);
    query.addBindValue(batch.categoryNameSnapshot);
    query.addBindValue(batch.typeCodeSnapshot);
    query.addBindValue(batch.locationNameSnapshot);
    query.addBindValue(batch.confirmedAt.toString(Qt::ISODate));
    query.addBindValue(batch.receivedByName);
    if (!query.exec())
        return Result<EntityId>::failure(databaseError(query));
    return Result<EntityId>::success(query.lastInsertId().toLongLong());
}

Result<bool> DocumentRepository::updateInbound(const InboundBatch &batch)
{
    const auto current = inboundById(batch.id);
    if (!current.ok())
        return Result<bool>::failure(current.error());
    const auto number = prepareNumber(batch.batchNo.trimmed().isEmpty()
            ? current.value().batchNo : batch.batchNo,
        QStringLiteral("B"), batch.businessAt.date(), batch.id);
    if (!number.ok())
        return Result<bool>::failure(number.error());
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "UPDATE inbound_batches SET batch_no=?,product_type_id=?,quantity=?,initial_location_id=?,"
        "business_at=?,note=?,material_model=?,received_by_id=?,category_name_snapshot=?,"
        "type_code_snapshot=?,location_name_snapshot=?,confirmed_at=?,received_by_name_snapshot=?,"
        "status='Confirmed',cancelled_at=NULL WHERE id=?"));
    query.addBindValue(number.value());
    query.addBindValue(batch.productTypeId);
    query.addBindValue(batch.quantity);
    query.addBindValue(batch.initialLocationId);
    query.addBindValue(batch.businessAt.toString(Qt::ISODate));
    query.addBindValue(batch.note.isNull() ? QStringLiteral("") : batch.note);
    query.addBindValue(batch.materialModel);
    query.addBindValue(batch.receivedById);
    query.addBindValue(batch.categoryNameSnapshot);
    query.addBindValue(batch.typeCodeSnapshot);
    query.addBindValue(batch.locationNameSnapshot);
    query.addBindValue(batch.confirmedAt.toString(Qt::ISODate));
    query.addBindValue(batch.receivedByName);
    query.addBindValue(batch.id);
    if (!query.exec())
        return Result<bool>::failure(databaseError(query));
    return Result<bool>::success(true);
}

Result<InboundBatch> DocumentRepository::inboundById(EntityId id) const
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "SELECT b.id,b.batch_no,b.product_type_id,b.quantity,b.initial_location_id,b.business_at,b.status,"
        "b.note,b.category_name_snapshot,b.type_code_snapshot,b.location_name_snapshot,"
        "b.created_at,b.confirmed_at,b.cancelled_at,b.material_model,b.received_by_id,"
        "b.received_by_name_snapshot"
        " FROM inbound_batches b WHERE b.id=?"));
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
        "SELECT b.id,b.batch_no,b.product_type_id,b.quantity,b.initial_location_id,b.business_at,b.status,"
        "b.note,b.category_name_snapshot,b.type_code_snapshot,b.location_name_snapshot,"
        "b.created_at,b.confirmed_at,b.cancelled_at,b.material_model,b.received_by_id,"
        "b.received_by_name_snapshot"
        " FROM inbound_batches b "
        "ORDER BY b.business_at DESC,b.created_at DESC,b.id DESC"));
    if (!query.exec()) return Result<QVector<InboundBatch>>::failure(databaseError(query));
    QVector<InboundBatch> rows;
    while (query.next()) rows.append(readBatch(query));
    if (query.lastError().isValid()) return Result<QVector<InboundBatch>>::failure(databaseError(query));
    return Result<QVector<InboundBatch>>::success(rows);
}

Result<bool> DocumentRepository::updateBatchSnapshots(EntityId batchId)
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "UPDATE logistics_document_lines SET "
        "batch_no_snapshot=(SELECT batch_no FROM inbound_batches WHERE id=?),"
        "category_name_snapshot=(SELECT category_name_snapshot FROM inbound_batches WHERE id=?),"
        "type_code_snapshot=(SELECT type_code_snapshot FROM inbound_batches WHERE id=?) "
        "WHERE batch_id=?"));
    for (int i = 0; i < 4; ++i)
        query.addBindValue(batchId);
    if (!query.exec())
        return Result<bool>::failure(databaseError(query));
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

Result<EntityId> DocumentRepository::insertLogistics(const LogisticsDocument &document)
{
    const auto number = prepareNumber(document.documentNo, numberPrefix(document.type),
                                      document.businessAt.date());
    if (!number.ok())
        return Result<EntityId>::failure(number.error());
    QSqlQuery header(database_.connection());
    header.prepare(QStringLiteral(
        "INSERT INTO logistics_documents(document_no,type,source_location_id,destination_location_id,"
        "business_at,note,created_at,outbound_confirmed_by_id,source_name_snapshot,"
        "destination_name_snapshot,confirmed_at,outbound_confirmed_by_name_snapshot,status) "
        "VALUES(?,?,?,?,?,?,?,NULLIF(?,0),?,?,?,?,'Confirmed')"));
    header.addBindValue(number.value());
    header.addBindValue(typeText(document.type));
    header.addBindValue(document.sourceLocationId);
    header.addBindValue(document.destinationLocationId);
    header.addBindValue(document.businessAt.toString(Qt::ISODate));
    header.addBindValue(document.note.isNull() ? QStringLiteral("") : document.note);
    header.addBindValue(document.createdAt.toString(Qt::ISODate));
    header.addBindValue(document.outboundConfirmedById);
    header.addBindValue(document.sourceNameSnapshot);
    header.addBindValue(document.destinationNameSnapshot);
    header.addBindValue(document.confirmedAt.toString(Qt::ISODate));
    header.addBindValue(document.outboundConfirmedByName.isNull()
        ? QStringLiteral("") : document.outboundConfirmedByName);
    if (!header.exec())
        return Result<EntityId>::failure(databaseError(header));
    const EntityId id = header.lastInsertId().toLongLong();
    const auto lines = insertLogisticsLines(id, document.lines);
    if (!lines.ok())
        return Result<EntityId>::failure(lines.error());
    return Result<EntityId>::success(id);
}

Result<bool> DocumentRepository::updateLogistics(const LogisticsDocument &document)
{
    const auto current = logisticsById(document.id);
    if (!current.ok())
        return Result<bool>::failure(current.error());
    const auto number = prepareNumber(document.documentNo.trimmed().isEmpty()
            ? current.value().documentNo : document.documentNo,
        numberPrefix(document.type), document.businessAt.date(), document.id);
    if (!number.ok())
        return Result<bool>::failure(number.error());
    QSqlQuery header(database_.connection());
    header.prepare(QStringLiteral(
        "UPDATE logistics_documents SET document_no=?,source_location_id=?,destination_location_id=?,"
        "business_at=?,note=?,outbound_confirmed_by_id=NULLIF(?,0),source_name_snapshot=?,"
        "destination_name_snapshot=?,confirmed_at=?,outbound_confirmed_by_name_snapshot=?,"
        "status='Confirmed',cancelled_at=NULL WHERE id=?"));
    header.addBindValue(number.value());
    header.addBindValue(document.sourceLocationId);
    header.addBindValue(document.destinationLocationId);
    header.addBindValue(document.businessAt.toString(Qt::ISODate));
    header.addBindValue(document.note.isNull() ? QStringLiteral("") : document.note);
    header.addBindValue(document.outboundConfirmedById);
    header.addBindValue(document.sourceNameSnapshot);
    header.addBindValue(document.destinationNameSnapshot);
    header.addBindValue(document.confirmedAt.toString(Qt::ISODate));
    header.addBindValue(document.outboundConfirmedByName.isNull()
        ? QStringLiteral("") : document.outboundConfirmedByName);
    header.addBindValue(document.id);
    if (!header.exec())
        return Result<bool>::failure(databaseError(header));
    QSqlQuery remove(database_.connection());
    remove.prepare(QStringLiteral("DELETE FROM logistics_document_lines WHERE document_id=?"));
    remove.addBindValue(document.id);
    if (!remove.exec())
        return Result<bool>::failure(databaseError(remove));
    return insertLogisticsLines(document.id, document.lines);
}

Result<bool> DocumentRepository::insertLogisticsLines(EntityId documentId,
                                                       const QList<LogisticsLine> &lines)
{
    for (const auto &line : lines) {
        QSqlQuery query(database_.connection());
        query.prepare(QStringLiteral(
            "INSERT INTO logistics_document_lines(document_id,batch_id,quantity,product_model_id,"
            "batch_no_snapshot,category_name_snapshot,type_code_snapshot,model_code_snapshot) "
            "VALUES(?,?,?,NULLIF(?,0),?,?,?,?)"));
        query.addBindValue(documentId);
        query.addBindValue(line.batchId);
        query.addBindValue(line.quantity);
        query.addBindValue(line.productModelId);
        query.addBindValue(line.batchNoSnapshot);
        query.addBindValue(line.categoryNameSnapshot);
        query.addBindValue(line.typeCodeSnapshot);
        query.addBindValue(line.modelCodeSnapshot.isNull() ? QStringLiteral("") : line.modelCodeSnapshot);
        if (!query.exec())
            return Result<bool>::failure(databaseError(query));
    }
    return Result<bool>::success(true);
}

Result<LogisticsDocument> DocumentRepository::logisticsById(EntityId id) const
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "SELECT d.id,d.document_no,d.type,d.source_location_id,d.destination_location_id,d.business_at,"
        "d.status,d.note,d.source_name_snapshot,d.destination_name_snapshot,d.created_at,d.confirmed_at,"
        "d.cancelled_at,d.outbound_confirmed_by_id,"
        "d.outbound_confirmed_by_name_snapshot"
        " FROM logistics_documents d WHERE d.id=?"));
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
        "l.batch_no_snapshot,l.category_name_snapshot,l.type_code_snapshot,"
        "l.product_model_id,l.model_code_snapshot,b.material_model "
        "FROM logistics_document_lines l JOIN inbound_batches b ON b.id=l.batch_id "
        "WHERE l.document_id=? ORDER BY l.id"));
    lines.addBindValue(id);
    if (!lines.exec())
        return Result<LogisticsDocument>::failure(databaseError(lines));
    while (lines.next())
        document.lines.append(readLogisticsLine(lines));
    if (lines.lastError().isValid())
        return Result<LogisticsDocument>::failure(databaseError(lines));
    return Result<LogisticsDocument>::success(document);
}

Result<QVector<LogisticsDocument>> DocumentRepository::logisticsList(DocumentType type) const
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "SELECT d.id,d.document_no,d.type,d.source_location_id,d.destination_location_id,d.business_at,"
        "d.status,d.note,d.source_name_snapshot,d.destination_name_snapshot,d.created_at,d.confirmed_at,"
        "d.cancelled_at,d.outbound_confirmed_by_id,"
        "d.outbound_confirmed_by_name_snapshot"
        " FROM logistics_documents d WHERE d.type=? "
        "ORDER BY d.business_at DESC,d.created_at DESC,d.id DESC"));
    query.addBindValue(typeText(type));
    if (!query.exec()) return Result<QVector<LogisticsDocument>>::failure(databaseError(query));
    QVector<LogisticsDocument> rows;
    while (query.next()) rows.append(readLogistics(query));
    if (query.lastError().isValid()) return Result<QVector<LogisticsDocument>>::failure(databaseError(query));
    return Result<QVector<LogisticsDocument>>::success(rows);
}

Result<QVector<LogisticsDocument>> DocumentRepository::logisticsForBatch(EntityId batchId) const
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "SELECT DISTINCT d.id,d.document_no,d.type,d.source_location_id,d.destination_location_id,"
        "d.business_at,d.status,d.note,d.source_name_snapshot,d.destination_name_snapshot,"
        "d.created_at,d.confirmed_at,d.cancelled_at,d.outbound_confirmed_by_id,"
        "d.outbound_confirmed_by_name_snapshot"
        " FROM logistics_documents d "
        "JOIN logistics_document_lines line ON line.document_id=d.id "
        "WHERE line.batch_id=? ORDER BY d.business_at,d.created_at,d.id"));
    query.addBindValue(batchId);
    if (!query.exec())
        return Result<QVector<LogisticsDocument>>::failure(databaseError(query));
    QVector<LogisticsDocument> rows;
    while (query.next()) {
        LogisticsDocument document = readLogistics(query);
        QSqlQuery lines(database_.connection());
        lines.prepare(QStringLiteral(
            "SELECT l.id,l.batch_id,l.quantity,b.product_type_id,"
            "l.batch_no_snapshot,l.category_name_snapshot,l.type_code_snapshot,"
            "l.product_model_id,l.model_code_snapshot,b.material_model "
            "FROM logistics_document_lines l JOIN inbound_batches b ON b.id=l.batch_id "
            "WHERE l.document_id=? ORDER BY l.id"));
        lines.addBindValue(document.id);
        if (!lines.exec())
            return Result<QVector<LogisticsDocument>>::failure(databaseError(lines));
        while (lines.next())
            document.lines.append(readLogisticsLine(lines));
        if (lines.lastError().isValid())
            return Result<QVector<LogisticsDocument>>::failure(databaseError(lines));
        rows.append(document);
    }
    if (query.lastError().isValid())
        return Result<QVector<LogisticsDocument>>::failure(databaseError(query));
    return Result<QVector<LogisticsDocument>>::success(rows);
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
