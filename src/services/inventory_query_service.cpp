#include "inventory_query_service.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {
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
        "b.type_code_snapshot,SUM(e.quantity_delta) "
        "FROM inbound_batches b JOIN inventory_entries e ON e.batch_id=b.id "
        "WHERE e.location_id=? GROUP BY b.id,b.product_type_id,b.batch_no,"
        "b.category_name_snapshot,b.type_code_snapshot HAVING SUM(e.quantity_delta)>0 "
        "ORDER BY b.batch_no,b.id"));
    q.addBindValue(locationId);
    if (!q.exec())
        return Result<QVector<AvailableBatch>>::failure(error(q));
    QVector<AvailableBatch> rows;
    while (q.next()) {
        rows.append({q.value(0).toLongLong(), q.value(1).toLongLong(), q.value(2).toString(),
                     q.value(3).toString(), q.value(4).toString(), q.value(5).toLongLong()});
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
        "SUM(e.quantity_delta) AS total_quantity FROM inbound_batches b "
        "JOIN inventory_entries e ON e.batch_id=b.id JOIN locations l ON l.id=e.location_id "
        "AND l.counts_as_inventory=1 GROUP BY b.id,b.batch_no,b.category_name_snapshot,"
        "b.type_code_snapshot HAVING SUM(e.quantity_delta)<>0 ORDER BY b.batch_no,b.id"));
    if (!q.exec())
        return Result<QVector<BatchBalance>>::failure(error(q));
    QVector<BatchBalance> rows;
    while (q.next())
        rows.append({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString(),
                     q.value(3).toString(), q.value(4).toLongLong()});
    if (q.lastError().isValid())
        return Result<QVector<BatchBalance>>::failure(error(q));
    return Result<QVector<BatchBalance>>::success(rows);
}

Result<QVector<BatchBalance>> InventoryQueryService::historicalBatches() const
{
    QSqlQuery q(database_.connection());
    q.prepare(QStringLiteral(
        "SELECT b.id,b.batch_no,b.category_name_snapshot,b.type_code_snapshot,"
        "COALESCE(SUM(CASE WHEN l.counts_as_inventory=1 THEN e.quantity_delta ELSE 0 END),0) "
        "FROM inbound_batches b LEFT JOIN inventory_entries e ON e.batch_id=b.id "
        "LEFT JOIN locations l ON l.id=e.location_id WHERE b.status<>'Draft' "
        "GROUP BY b.id,b.batch_no,b.category_name_snapshot,b.type_code_snapshot "
        "ORDER BY b.batch_no,b.id"));
    if (!q.exec())
        return Result<QVector<BatchBalance>>::failure(error(q));
    QVector<BatchBalance> rows;
    while (q.next())
        rows.append({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString(),
                     q.value(3).toString(), q.value(4).toLongLong()});
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

Result<QVector<BatchLocationBalance>> InventoryQueryService::batchLocationBalances(EntityId batchId) const
{
    QSqlQuery q(database_.connection());
    q.prepare(QStringLiteral(
        "WITH balances AS (SELECT location_id,SUM(quantity_delta) quantity "
        "FROM inventory_entries WHERE batch_id=? GROUP BY location_id "
        "HAVING SUM(quantity_delta)>0) SELECT balances.location_id,COALESCE((SELECT CASE "
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
        "COALESCE(line.type_code_snapshot,b.type_code_snapshot) "
        "FROM inventory_entries e LEFT JOIN inbound_batches b ON e.source_type='Inbound' AND b.id=e.source_id "
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
                     q.value(9).toString()});
    }
    if (q.lastError().isValid())
        return Result<QVector<BatchTimelineItem>>::failure(error(q));
    return Result<QVector<BatchTimelineItem>>::success(rows);
}

Result<QVector<ProductTypeShipment>> InventoryQueryService::productTypeShipmentSummary(
    const DateRange &range) const
{
    QSqlQuery q(database_.connection());
    q.prepare(QStringLiteral(
        "SELECT line.category_name_snapshot,line.type_code_snapshot,SUM(line.quantity) "
        "FROM logistics_documents d JOIN logistics_document_lines line ON line.document_id=d.id "
        "WHERE d.type='Outbound' AND d.status='Confirmed' AND d.business_at>=? "
        "AND d.business_at<? GROUP BY line.category_name_snapshot,line.type_code_snapshot "
        "ORDER BY line.category_name_snapshot,line.type_code_snapshot"));
    q.addBindValue(range.from.toString(Qt::ISODate));
    q.addBindValue(range.to.addDays(1).toString(Qt::ISODate));
    if (!q.exec())
        return Result<QVector<ProductTypeShipment>>::failure(error(q));
    QVector<ProductTypeShipment> rows;
    while (q.next())
        rows.append({q.value(0).toString(), q.value(1).toString(), q.value(2).toLongLong()});
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

    QSqlQuery q(database_.connection());
    q.prepare(QStringLiteral(
        "SELECT source_document,business_type,business_at,source_name,destination_name,quantity "
        "FROM (SELECT b.batch_no source_document,'入库' business_type,b.business_at,'' source_name,"
        "b.location_name_snapshot destination_name,b.quantity quantity,b.created_at sort_created,b.id sort_id "
        "FROM inbound_batches b WHERE b.status='Confirmed' UNION ALL SELECT d.document_no,"
        "CASE d.type WHEN 'Transfer' THEN '调拨' ELSE '出库' END,d.business_at,d.source_name_snapshot,"
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
