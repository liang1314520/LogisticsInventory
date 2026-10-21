#include "inventory_query_service.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {
const QString negativeBalancesSql = QStringLiteral(
    "SELECT e.batch_id,e.location_id,SUM(e.quantity_delta) quantity "
    "FROM inventory_entries e JOIN locations l ON l.id=e.location_id "
    "WHERE l.counts_as_inventory=1 GROUP BY e.batch_id,e.location_id "
    "HAVING SUM(e.quantity_delta)<0");

ServiceError error(const QSqlQuery &q)
{
    return {QStringLiteral("database"), q.lastError().text(), {}};
}

QDateTime dateTime(const QVariant &value)
{
    return QDateTime::fromString(value.toString(), Qt::ISODate);
}

Result<qint64> scalar(Database &db, const QString &sql,
                      const QVariantList &values = {})
{
    QSqlQuery q(db.connection());
    q.prepare(sql);
    for (const auto &value : values)
        q.addBindValue(value);
    if (!q.exec() || !q.next())
        return Result<qint64>::failure(error(q));
    bool ok = false;
    const qint64 value = q.value(0).toLongLong(&ok);
    if (!ok)
        return Result<qint64>::failure({QStringLiteral("database"),
                                        QStringLiteral("统计数量读取失败"), {}});
    return Result<qint64>::success(value);
}

QString monthStart(const QDate &month)
{
    return QDate(month.year(), month.month(), 1).toString(Qt::ISODate);
}

QString nextMonth(const QDate &month)
{
    return QDate(month.year(), month.month(), 1).addMonths(1).toString(Qt::ISODate);
}
}

InventoryQueryService::InventoryQueryService(Database &database) : database_(database) {}

Result<qint64> InventoryQueryService::balance(EntityId batchId, EntityId locationId) const
{
    return scalar(database_, QStringLiteral(
                      "SELECT COALESCE(SUM(quantity_delta),0) FROM inventory_entries "
                      "WHERE batch_id=? AND location_id=?"),
                  {batchId, locationId});
}

Result<qint64> InventoryQueryService::batchRemaining(EntityId batchId) const
{
    return scalar(database_, QStringLiteral(
                      "SELECT COALESCE(SUM(e.quantity_delta),0) FROM inventory_entries e "
                      "JOIN locations l ON l.id=e.location_id WHERE e.batch_id=? "
                      "AND l.counts_as_inventory=1"),
                  {batchId});
}

Result<QVector<AvailableBatch>> InventoryQueryService::availableBatches(EntityId locationId) const
{
    QSqlQuery q(database_.connection());
    q.prepare(QStringLiteral(
        "SELECT b.id,b.product_type_id,b.batch_no,b.category_name_snapshot,"
        "b.type_code_snapshot,COALESCE(SUM(e.quantity_delta),0),b.material_model "
        "FROM inbound_batches b JOIN product_types p ON p.id=b.product_type_id "
        "JOIN product_categories c ON c.id=p.category_id "
        "LEFT JOIN inventory_entries e ON e.batch_id=b.id AND e.location_id=? "
        "WHERE b.status='Confirmed' AND p.enabled=1 AND c.enabled=1 "
        "GROUP BY b.id,b.product_type_id,b.batch_no,"
        "b.category_name_snapshot,b.type_code_snapshot,b.material_model "
        "ORDER BY b.batch_no,b.id"));
    q.addBindValue(locationId);
    if (!q.exec())
        return Result<QVector<AvailableBatch>>::failure(error(q));
    QVector<AvailableBatch> rows;
    while (q.next()) {
        rows.append({q.value(0).toLongLong(), q.value(1).toLongLong(), q.value(2).toString(),
                     q.value(3).toString(), q.value(4).toString(), q.value(5).toLongLong(),
                     q.value(6).toString()});
    }
    if (q.lastError().isValid())
        return Result<QVector<AvailableBatch>>::failure(error(q));
    return Result<QVector<AvailableBatch>>::success(rows);
}

Result<QVector<BatchBalance>> InventoryQueryService::batchBalances() const
{
    QSqlQuery q(database_.connection());
    q.prepare(QStringLiteral(
        "SELECT b.id,b.batch_no,b.category_name_snapshot,b.type_code_snapshot,"
        "SUM(e.quantity_delta) AS total_quantity,b.material_model FROM inbound_batches b "
        "JOIN inventory_entries e ON e.batch_id=b.id JOIN locations l ON l.id=e.location_id "
        "AND l.counts_as_inventory=1 GROUP BY b.id,b.batch_no,b.category_name_snapshot,"
        "b.type_code_snapshot,b.material_model HAVING SUM(e.quantity_delta)<>0 ORDER BY b.batch_no,b.id"));
    if (!q.exec())
        return Result<QVector<BatchBalance>>::failure(error(q));
    QVector<BatchBalance> rows;
    while (q.next())
        rows.append({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString(),
                     q.value(3).toString(), q.value(4).toLongLong(), q.value(5).toString()});
    if (q.lastError().isValid())
        return Result<QVector<BatchBalance>>::failure(error(q));
    return Result<QVector<BatchBalance>>::success(rows);
}

Result<QVector<BatchBalance>> InventoryQueryService::historicalBatches() const
{
    QSqlQuery q(database_.connection());
    q.prepare(QStringLiteral(
        "SELECT b.id,b.batch_no,b.category_name_snapshot,b.type_code_snapshot,"
        "COALESCE(SUM(CASE WHEN l.counts_as_inventory=1 THEN e.quantity_delta ELSE 0 END),0),b.material_model "
        "FROM inbound_batches b LEFT JOIN inventory_entries e ON e.batch_id=b.id "
        "LEFT JOIN locations l ON l.id=e.location_id "
        "GROUP BY b.id,b.batch_no,b.category_name_snapshot,b.type_code_snapshot,b.material_model "
        "ORDER BY b.batch_no,b.id"));
    if (!q.exec())
        return Result<QVector<BatchBalance>>::failure(error(q));
    QVector<BatchBalance> rows;
    while (q.next())
        rows.append({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString(),
                     q.value(3).toString(), q.value(4).toLongLong(), q.value(5).toString()});
    if (q.lastError().isValid())
        return Result<QVector<BatchBalance>>::failure(error(q));
    return Result<QVector<BatchBalance>>::success(rows);
}

Result<QVector<LocationBalance>> InventoryQueryService::locationBalances() const
{
    QSqlQuery q(database_.connection());
    q.prepare(QStringLiteral(
        "WITH balances AS (SELECT e.location_id,SUM(e.quantity_delta) quantity "
        "FROM inventory_entries e JOIN locations l ON l.id=e.location_id "
        "AND l.counts_as_inventory=1 GROUP BY e.location_id) "
        "SELECT balances.location_id,COALESCE((SELECT CASE "
        "WHEN latest.source_type='Inbound' THEN b.location_name_snapshot "
        "WHEN latest.location_id=d.source_location_id THEN d.source_name_snapshot "
        "WHEN latest.location_id=d.destination_location_id THEN d.destination_name_snapshot END "
        "FROM inventory_entries latest LEFT JOIN inbound_batches b ON latest.source_type='Inbound' "
        "AND b.id=latest.source_id LEFT JOIN logistics_documents d ON latest.source_type='Logistics' "
        "AND d.id=latest.source_id WHERE latest.location_id=balances.location_id "
        "ORDER BY latest.business_at DESC,latest.created_at DESC,latest.id DESC LIMIT 1),l.name),"
        "balances.quantity FROM balances JOIN locations l ON l.id=balances.location_id "
        "WHERE balances.quantity<>0 ORDER BY balances.location_id"));
    if (!q.exec())
        return Result<QVector<LocationBalance>>::failure(error(q));
    QVector<LocationBalance> rows;
    while (q.next())
        rows.append({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toLongLong()});
    if (q.lastError().isValid())
        return Result<QVector<LocationBalance>>::failure(error(q));
    return Result<QVector<LocationBalance>>::success(rows);
}

Result<qint64> InventoryQueryService::negativeBalanceCount() const
{
    return scalar(database_, QStringLiteral("SELECT COUNT(*) FROM (%1)").arg(negativeBalancesSql));
}

Result<QVector<NegativeBalance>> InventoryQueryService::negativeBalances() const
{
    QSqlQuery q(database_.connection());
    if (!q.exec(QStringLiteral(
            "WITH balances AS (%1) SELECT n.batch_id,n.location_id,b.batch_no,l.name,n.quantity "
            "FROM balances n JOIN inbound_batches b ON b.id=n.batch_id "
            "JOIN locations l ON l.id=n.location_id ORDER BY b.batch_no,l.name,n.location_id")
            .arg(negativeBalancesSql)))
        return Result<QVector<NegativeBalance>>::failure(error(q));
    QVector<NegativeBalance> rows;
    while (q.next())
        rows.append({q.value(0).toLongLong(), q.value(1).toLongLong(), q.value(2).toString(),
                     q.value(3).toString(), q.value(4).toLongLong()});
    if (q.lastError().isValid())
        return Result<QVector<NegativeBalance>>::failure(error(q));
    return Result<QVector<NegativeBalance>>::success(rows);
}

Result<QVector<BatchLocationBalance>> InventoryQueryService::batchLocationBalances(EntityId batchId) const
{
    QSqlQuery q(database_.connection());
    q.prepare(QStringLiteral(
        "WITH balances AS (SELECT location_id,SUM(quantity_delta) quantity "
        "FROM inventory_entries WHERE batch_id=? GROUP BY location_id "
        "HAVING SUM(quantity_delta)<>0) SELECT balances.location_id,COALESCE((SELECT CASE "
        "WHEN latest.source_type='Inbound' THEN b.location_name_snapshot "
        "WHEN latest.location_id=d.source_location_id THEN d.source_name_snapshot "
        "WHEN latest.location_id=d.destination_location_id THEN d.destination_name_snapshot END "
        "FROM inventory_entries latest LEFT JOIN inbound_batches b ON latest.source_type='Inbound' "
        "AND b.id=latest.source_id LEFT JOIN logistics_documents d ON latest.source_type='Logistics' "
        "AND d.id=latest.source_id WHERE latest.batch_id=? AND latest.location_id=balances.location_id "
        "ORDER BY latest.business_at DESC,latest.created_at DESC,latest.id DESC LIMIT 1),l.name),"
        "balances.quantity FROM balances JOIN locations l ON l.id=balances.location_id "
        "ORDER BY balances.location_id"));
    q.addBindValue(batchId);
    q.addBindValue(batchId);
    if (!q.exec())
        return Result<QVector<BatchLocationBalance>>::failure(error(q));
    QVector<BatchLocationBalance> rows;
    while (q.next())
        rows.append({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toLongLong()});
    if (q.lastError().isValid())
        return Result<QVector<BatchLocationBalance>>::failure(error(q));
    return Result<QVector<BatchLocationBalance>>::success(rows);
}

Result<QVector<BatchTimelineItem>> InventoryQueryService::batchTimeline(EntityId batchId) const
{
    QSqlQuery q(database_.connection());
    q.prepare(QStringLiteral(
        "SELECT e.id,CASE WHEN e.source_type='Inbound' THEN b.batch_no ELSE d.document_no END,"
        "CASE WHEN e.source_type='Inbound' AND e.reversal_of_entry_id IS NOT NULL THEN '入库冲销' "
        "WHEN e.source_type='Inbound' THEN '入库' "
        "WHEN d.type='Return' AND e.location_id=d.source_location_id AND e.reversal_of_entry_id IS NULL THEN '退回发出' "
        "WHEN d.type='Return' AND e.location_id=d.destination_location_id AND e.reversal_of_entry_id IS NULL THEN '退回接收' "
        "WHEN d.type='Return' AND e.location_id=d.source_location_id THEN '退回发出冲销' "
        "WHEN d.type='Return' AND e.location_id=d.destination_location_id THEN '退回接收冲销' "
        "WHEN d.type='Transfer' AND e.location_id=d.source_location_id AND e.reversal_of_entry_id IS NULL THEN '调拨发出' "
        "WHEN d.type='Transfer' AND e.location_id=d.destination_location_id AND e.reversal_of_entry_id IS NULL THEN '调拨接收' "
        "WHEN d.type='Transfer' AND e.location_id=d.source_location_id THEN '调拨发出冲销' "
        "WHEN d.type='Transfer' AND e.location_id=d.destination_location_id THEN '调拨接收冲销' "
        "WHEN e.reversal_of_entry_id IS NOT NULL THEN '出库冲销' ELSE '出库' END,e.quantity_delta,"
        "CASE WHEN e.source_type='Inbound' THEN b.location_name_snapshot "
        "WHEN e.location_id=d.source_location_id THEN d.source_name_snapshot "
        "WHEN e.location_id=d.destination_location_id THEN d.destination_name_snapshot END,"
        "e.business_at,e.created_at,COALESCE(line.batch_no_snapshot,b.batch_no),"
        "COALESCE(line.category_name_snapshot,b.category_name_snapshot),"
        "COALESCE(line.type_code_snapshot,b.type_code_snapshot),line.model_code_snapshot,"
        "CASE WHEN e.source_type='Inbound' THEN b.status ELSE d.status END,"
        "e.reversal_of_entry_id IS NOT NULL,b.material_model "
        "FROM inventory_entries e JOIN inbound_batches b ON b.id=e.batch_id "
        "LEFT JOIN logistics_documents d ON e.source_type='Logistics' AND d.id=e.source_id "
        "LEFT JOIN logistics_document_lines line ON line.id=e.source_line_id WHERE e.batch_id=? "
        "ORDER BY e.business_at,e.created_at,e.id"));
    q.addBindValue(batchId);
    if (!q.exec())
        return Result<QVector<BatchTimelineItem>>::failure(error(q));
    QVector<BatchTimelineItem> rows;
    while (q.next()) {
        rows.append({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString(),
                     q.value(3).toLongLong(), q.value(4).toString(), dateTime(q.value(5)),
                     dateTime(q.value(6)), q.value(7).toString(), q.value(8).toString(),
                     q.value(9).toString(), q.value(10).toString(),
                     q.value(11).toString() == QStringLiteral("Cancelled") ? DocumentStatus::Cancelled
                                                                             : DocumentStatus::Confirmed,
                     q.value(12).toBool(), q.value(13).toString()});
    }
    if (q.lastError().isValid())
        return Result<QVector<BatchTimelineItem>>::failure(error(q));
    return Result<QVector<BatchTimelineItem>>::success(rows);
}

Result<QVector<ProductTypeShipment>> InventoryQueryService::productTypeShipmentSummary(
    const DateRange &range) const
{
    if (!range.from.isValid() || !range.to.isValid() ||
        !range.fromTime.isValid() || !range.toTime.isValid())
        return Result<QVector<ProductTypeShipment>>::failure({QStringLiteral("invalid_range"),
            QStringLiteral("请选择有效的起止日期和时间"), QStringLiteral("dateRange")});
    const QDateTime from(range.from, QTime(range.fromTime.hour(), range.fromTime.minute()));
    const QDateTime to(range.to, QTime(range.toTime.hour(), range.toTime.minute()));
    if (!from.isValid() || !to.isValid() || from > to)
        return Result<QVector<ProductTypeShipment>>::failure({QStringLiteral("invalid_range"),
            QStringLiteral("开始时间不能晚于结束时间，且必须为有效的本地时间"),
            QStringLiteral("dateRange")});
    QSqlQuery q(database_.connection());
    q.prepare(QStringLiteral(
        "WITH documents AS (SELECT d.*,CASE "
        "WHEN substr(d.business_at,-1)='Z' OR substr(d.business_at,-6,1) IN ('+','-') "
        "THEN julianday(d.business_at) ELSE julianday(d.business_at,'utc') END AS business_instant "
        "FROM logistics_documents d WHERE d.status='Confirmed' AND d.type IN ('Outbound','Return')) "
        "SELECT line.category_name_snapshot,line.type_code_snapshot,"
        "SUM(CASE WHEN d.type='Outbound' THEN line.quantity ELSE 0 END),"
        "COALESCE(line.model_code_snapshot,''),"
        "SUM(CASE WHEN d.type='Return' THEN line.quantity ELSE 0 END),b.material_model "
        "FROM documents d JOIN logistics_document_lines line ON line.document_id=d.id "
        "JOIN inbound_batches b ON b.id=line.batch_id "
        "WHERE d.business_instant>=julianday(?) AND d.business_instant<julianday(?) "
        "GROUP BY line.category_name_snapshot,line.type_code_snapshot,"
        "COALESCE(line.model_code_snapshot,''),b.material_model "
        "ORDER BY line.category_name_snapshot,line.type_code_snapshot,b.material_model,"
        "COALESCE(line.model_code_snapshot,'')"));
    q.addBindValue(from.toUTC().toString(Qt::ISODate));
    q.addBindValue(to.addSecs(60).toUTC().toString(Qt::ISODate));
    if (!q.exec())
        return Result<QVector<ProductTypeShipment>>::failure(error(q));
    QVector<ProductTypeShipment> rows;
    while (q.next())
        rows.append({q.value(0).toString(), q.value(1).toString(), q.value(2).toLongLong(),
                     q.value(3).toString(), q.value(4).toLongLong(), q.value(5).toString()});
    if (q.lastError().isValid())
        return Result<QVector<ProductTypeShipment>>::failure(error(q));
    return Result<QVector<ProductTypeShipment>>::success(rows);
}

Result<DashboardSummary> InventoryQueryService::dashboardSummary(const QDate &month) const
{
    DashboardSummary out;
    const QString from = monthStart(month);
    const QString to = nextMonth(month);
    auto total = scalar(database_, QStringLiteral(
                             "SELECT COALESCE(SUM(e.quantity_delta),0) FROM inventory_entries e "
                             "JOIN locations l ON l.id=e.location_id WHERE l.counts_as_inventory=1"));
    if (!total.ok())
        return Result<DashboardSummary>::failure(total.error());
    out.totalInventory = total.value();
    auto company = scalar(database_, QStringLiteral(
                              "SELECT COALESCE(SUM(e.quantity_delta),0) FROM inventory_entries e "
                              "JOIN locations l ON l.id=e.location_id WHERE l.counts_as_inventory=1 "
                              "AND l.type='Company'"));
    if (!company.ok())
        return Result<DashboardSummary>::failure(company.error());
    out.companyInventory = company.value();
    auto factory = scalar(database_, QStringLiteral(
                              "SELECT COALESCE(SUM(e.quantity_delta),0) FROM inventory_entries e "
                              "JOIN locations l ON l.id=e.location_id WHERE l.counts_as_inventory=1 "
                              "AND l.type='Factory'"));
    if (!factory.ok())
        return Result<DashboardSummary>::failure(factory.error());
    out.factoryInventory = factory.value();
    auto inbound = scalar(database_, QStringLiteral(
                              "SELECT COALESCE(SUM(quantity),0) FROM inbound_batches "
                              "WHERE status='Confirmed' AND business_at>=? AND business_at<?"),
                          {from, to});
    if (!inbound.ok())
        return Result<DashboardSummary>::failure(inbound.error());
    out.monthlyInbound = inbound.value();
    auto outbound = scalar(database_, QStringLiteral(
                               "SELECT COALESCE(SUM(line.quantity),0) FROM logistics_documents d "
                               "JOIN logistics_document_lines line ON line.document_id=d.id "
                               "WHERE d.type='Outbound' AND d.status='Confirmed' "
                               "AND d.business_at>=? AND d.business_at<?"),
                           {from, to});
    if (!outbound.ok())
        return Result<DashboardSummary>::failure(outbound.error());
    out.monthlyOutbound = outbound.value();
    const auto returned = scalar(database_, QStringLiteral(
        "SELECT COALESCE(SUM(line.quantity),0) FROM logistics_documents d "
        "JOIN logistics_document_lines line ON line.document_id=d.id "
        "WHERE d.type='Return' AND d.status='Confirmed' "
        "AND d.business_at>=? AND d.business_at<?"),
        {from, to});
    if (!returned.ok()) return Result<DashboardSummary>::failure(returned.error());
    out.monthlyReturned = returned.value();
    const auto negatives = negativeBalanceCount();
    if (!negatives.ok()) return Result<DashboardSummary>::failure(negatives.error());
    out.negativeBalanceCount = negatives.value();

    QSqlQuery q(database_.connection());
    q.prepare(QStringLiteral(
        "SELECT source_document,business_type,business_at,source_name,destination_name,quantity "
        "FROM (SELECT b.batch_no source_document,'入库' business_type,b.business_at,'' source_name,"
        "b.location_name_snapshot destination_name,b.quantity quantity,b.created_at sort_created,b.id sort_id "
        "FROM inbound_batches b WHERE b.status='Confirmed' UNION ALL SELECT d.document_no,"
        "CASE d.type WHEN 'Transfer' THEN '调拨' WHEN 'Return' THEN '退回' ELSE '出库' END,d.business_at,d.source_name_snapshot,"
        "d.destination_name_snapshot,SUM(line.quantity),d.created_at,d.id FROM logistics_documents d "
        "JOIN logistics_document_lines line ON line.document_id=d.id WHERE d.status='Confirmed' "
        "GROUP BY d.id) ORDER BY business_at DESC,sort_created DESC,sort_id DESC LIMIT 10"));
    if (!q.exec())
        return Result<DashboardSummary>::failure(error(q));
    while (q.next()) {
        out.recentRecords.append({q.value(0).toString(), q.value(1).toString(), dateTime(q.value(2)),
                                  q.value(3).toString(), q.value(4).toString(),
                                  q.value(5).toLongLong()});
    }
    if (q.lastError().isValid())
        return Result<DashboardSummary>::failure(error(q));
    return Result<DashboardSummary>::success(out);
}
