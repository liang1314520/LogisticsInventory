#include "services/inventory_query_service.h"
#include "test_support.h"

#include <QFile>
#include <QSqlQuery>
#include <QtTest>
#include <functional>

class InventoryQueriesTest : public QObject {
    Q_OBJECT
private slots:
    void flowBalancesUseSnapshotsAndReversals();
    void batchBalancesExcludeNaturallyZeroBatch();
    void cancelledTransferReversalsUseEntryLocationsForSnapshots();
    void timelineHasExactOrderAndSnapshots();
    void statisticsExcludeTransfersAndCancelledOutbound();
    void collectionExecErrorsArePropagated();
    void collectionFetchErrorCheckIsPresent();
private:
    static bool run(Database &db, const QString &sql);
    static void seed(Database &db);
};

bool InventoryQueriesTest::run(Database &db, const QString &sql)
{
    QSqlQuery query(db.connection());
    return query.exec(sql);
}

void InventoryQueriesTest::seed(Database &db)
{
    QVERIFY(run(db, "INSERT INTO product_categories VALUES(1,'设备',1)"));
    QVERIFY(run(db, "INSERT INTO product_types VALUES(1,1,'NEW','已改名',1)"));
    QVERIFY(run(db, "INSERT INTO locations VALUES(1,'新公司名','Company',1,1),(2,'新甲厂名','Factory',1,1),(3,'乙厂','Factory',1,1),(4,'客户','Customer',0,1)"));
    QVERIFY(run(db, "INSERT INTO inbound_batches VALUES(1,'B820',1,820,1,'2026-09-01T08:00:00','Confirmed','','设备','NEW','旧公司','2026-09-01T07:00:00','2026-09-01T08:01:00',NULL)"));
    QVERIFY(run(db, "INSERT INTO inventory_entries VALUES(1,1,1,'Inbound',1,NULL,820,'2026-09-01T08:00:00','2026-09-01T08:01:00',NULL)"));
    QVERIFY(run(db, "INSERT INTO logistics_documents VALUES(1,'T-A','Transfer',1,2,'2026-09-02T09:00:00','Confirmed','','旧公司','甲厂快照','2026-09-02T08:00:00','2026-09-02T09:01:00',NULL),(2,'T-B','Transfer',1,3,'2026-09-02T10:00:00','Confirmed','','旧公司','乙厂快照','2026-09-02T08:30:00','2026-09-02T10:01:00',NULL),(3,'O-A','Outbound',2,4,'2026-09-03T10:00:00','Confirmed','','甲厂快照','客户快照','2026-09-03T09:00:00','2026-09-03T10:01:00',NULL),(4,'O-B','Outbound',3,4,'2026-09-04T10:00:00','Confirmed','','乙厂快照','客户快照','2026-09-04T09:00:00','2026-09-04T10:01:00',NULL),(5,'O-X','Outbound',3,4,'2026-09-05T10:00:00','Cancelled','','乙厂快照','客户快照','2026-09-05T09:00:00','2026-09-05T10:01:00','2026-09-06T10:00:00')"));
    QVERIFY(run(db, "INSERT INTO logistics_document_lines VALUES(1,1,1,500,'B820','设备','NEW'),(2,2,1,320,'B820','设备','NEW'),(3,3,1,250,'B820','设备','NEW'),(4,4,1,100,'B820','设备','NEW'),(5,5,1,20,'B820','设备','NEW')"));
    QVERIFY(run(db, "INSERT INTO inventory_entries VALUES(2,1,1,'Logistics',1,1,-500,'2026-09-02T09:00:00','2026-09-02T09:01:00',NULL),(3,1,2,'Logistics',1,1,500,'2026-09-02T09:00:00','2026-09-02T09:01:01',NULL),(4,1,1,'Logistics',2,2,-320,'2026-09-02T10:00:00','2026-09-02T10:01:00',NULL),(5,1,3,'Logistics',2,2,320,'2026-09-02T10:00:00','2026-09-02T10:01:01',NULL),(6,1,2,'Logistics',3,3,-250,'2026-09-03T10:00:00','2026-09-03T10:01:00',NULL),(7,1,3,'Logistics',4,4,-100,'2026-09-04T10:00:00','2026-09-04T10:01:00',NULL),(8,1,3,'Logistics',5,5,-20,'2026-09-05T10:00:00','2026-09-05T10:01:00',NULL),(9,1,3,'Logistics',5,5,20,'2026-09-05T10:00:00','2026-09-06T10:00:00',8)"));
}

void InventoryQueriesTest::flowBalancesUseSnapshotsAndReversals()
{
    TestDatabase db; QVERIFY(db.openAndMigrate()); seed(db.database());
    InventoryQueryService service(db.database());
    QCOMPARE(service.balance(1,1).value(), qint64(0));
    QCOMPARE(service.balance(1,2).value(), qint64(250));
    QCOMPARE(service.balance(1,3).value(), qint64(220));
    QCOMPARE(service.batchRemaining(1).value(), qint64(470));
    const auto batches = service.batchBalances(); QVERIFY(batches.ok()); QCOMPARE(batches.value().size(),1);
    QCOMPARE(batches.value().front().batchNo, QStringLiteral("B820")); QCOMPARE(batches.value().front().categoryName, QStringLiteral("设备")); QCOMPARE(batches.value().front().typeCode, QStringLiteral("NEW"));
    const auto locations = service.locationBalances(); QVERIFY(locations.ok()); QCOMPARE(locations.value().size(),2);
    QCOMPARE(locations.value().at(0).locationName, QStringLiteral("甲厂快照")); QCOMPARE(locations.value().at(0).quantity,qint64(250));
    QCOMPARE(locations.value().at(1).locationName, QStringLiteral("乙厂快照")); QCOMPARE(locations.value().at(1).quantity,qint64(220));
}

void InventoryQueriesTest::batchBalancesExcludeNaturallyZeroBatch()
{
    TestDatabase db; QVERIFY(db.openAndMigrate()); seed(db.database());
    QVERIFY(run(db.database(), "INSERT INTO inbound_batches VALUES(2,'B-ZERO',1,10,1,'2026-09-06T08:00:00','Confirmed','','设备','ZERO','旧公司','2026-09-06T07:00:00','2026-09-06T08:01:00',NULL)"));
    QVERIFY(run(db.database(), "INSERT INTO logistics_documents VALUES(6,'O-ZERO','Outbound',1,4,'2026-09-07T08:00:00','Confirmed','','旧公司','客户快照','2026-09-07T07:00:00','2026-09-07T08:01:00',NULL)"));
    QVERIFY(run(db.database(), "INSERT INTO logistics_document_lines VALUES(6,6,2,10,'B-ZERO','设备','ZERO')"));
    QVERIFY(run(db.database(), "INSERT INTO inventory_entries VALUES(10,2,1,'Inbound',2,NULL,10,'2026-09-06T08:00:00','2026-09-06T08:01:00',NULL),(11,2,1,'Logistics',6,6,-10,'2026-09-07T08:00:00','2026-09-07T08:01:00',NULL)"));
    InventoryQueryService service(db.database());
    const auto batches=service.batchBalances(); QVERIFY(batches.ok());
    QCOMPARE(batches.value().size(),1);
    QCOMPARE(batches.value().front().batchNo,QStringLiteral("B820"));
    QCOMPARE(batches.value().front().quantity,qint64(470));
}

void InventoryQueriesTest::cancelledTransferReversalsUseEntryLocationsForSnapshots()
{
    TestDatabase db; QVERIFY(db.openAndMigrate()); seed(db.database());
    QVERIFY(run(db.database(), "INSERT INTO logistics_documents VALUES(6,'T-X','Transfer',2,3,'2026-09-07T10:00:00','Cancelled','','甲厂冲销快照','乙厂冲销快照','2026-09-07T09:00:00','2026-09-07T10:01:00','2026-09-08T10:00:00')"));
    QVERIFY(run(db.database(), "INSERT INTO logistics_document_lines VALUES(6,6,1,10,'B820','设备','NEW')"));
    QVERIFY(run(db.database(), "INSERT INTO inventory_entries VALUES(10,1,2,'Logistics',6,6,-10,'2026-09-07T10:00:00','2026-09-07T10:01:00',NULL),(11,1,3,'Logistics',6,6,10,'2026-09-07T10:00:00','2026-09-07T10:01:01',NULL),(12,1,2,'Logistics',6,6,10,'2026-09-07T10:00:00','2026-09-08T10:00:00',10),(13,1,3,'Logistics',6,6,-10,'2026-09-07T10:00:00','2026-09-08T10:00:01',11)"));
    InventoryQueryService service(db.database());
    const auto timeline=service.batchTimeline(1); QVERIFY(timeline.ok()); QCOMPARE(timeline.value().size(),13);
    QCOMPARE(timeline.value().at(8).locationName,QStringLiteral("乙厂快照"));
    QCOMPARE(timeline.value().at(8).direction,QStringLiteral("出库冲销"));
    QCOMPARE(timeline.value().at(9).direction,QStringLiteral("调拨发出"));
    QCOMPARE(timeline.value().at(10).direction,QStringLiteral("调拨接收"));
    QCOMPARE(timeline.value().at(11).locationName,QStringLiteral("甲厂冲销快照"));
    QCOMPARE(timeline.value().at(11).direction,QStringLiteral("调拨发出冲销"));
    QCOMPARE(timeline.value().at(12).locationName,QStringLiteral("乙厂冲销快照"));
    QCOMPARE(timeline.value().at(12).direction,QStringLiteral("调拨接收冲销"));
}

void InventoryQueriesTest::timelineHasExactOrderAndSnapshots()
{
    TestDatabase db; QVERIFY(db.openAndMigrate()); seed(db.database());
    InventoryQueryService service(db.database());
    const auto result = service.batchTimeline(1); QVERIFY(result.ok()); QCOMPARE(result.value().size(),9);
    const auto &first = result.value().front(); QCOMPARE(first.sourceDocument, QStringLiteral("B820"));
    QCOMPARE(first.direction, QStringLiteral("入库")); QCOMPARE(first.locationName, QStringLiteral("旧公司"));
    QCOMPARE(result.value().at(1).sourceDocument, QStringLiteral("T-A")); QCOMPARE(result.value().at(1).locationName, QStringLiteral("旧公司"));
    QCOMPARE(result.value().at(1).direction,QStringLiteral("调拨发出"));
    QCOMPARE(result.value().at(2).locationName, QStringLiteral("甲厂快照"));
    QCOMPARE(result.value().at(2).direction,QStringLiteral("调拨接收"));
    QCOMPARE(result.value().at(5).direction,QStringLiteral("出库"));
    QCOMPARE(result.value().at(7).direction,QStringLiteral("出库"));
    QCOMPARE(result.value().at(7).quantityDelta, qint64(-20)); QCOMPARE(result.value().at(8).quantityDelta, qint64(20));
    QCOMPARE(result.value().at(8).locationName,QStringLiteral("乙厂快照"));
    QCOMPARE(result.value().at(8).direction,QStringLiteral("出库冲销"));
}

void InventoryQueriesTest::statisticsExcludeTransfersAndCancelledOutbound()
{
    TestDatabase db; QVERIFY(db.openAndMigrate()); seed(db.database()); InventoryQueryService service(db.database());
    const auto productType = service.productTypeShipmentSummary({QDate(2026,9,1),QDate(2026,9,30)}); QVERIFY(productType.ok()); QCOMPARE(productType.value().size(),1);
    QCOMPARE(productType.value().at(0).categoryName, QStringLiteral("设备")); QCOMPARE(productType.value().at(0).typeCode, QStringLiteral("NEW")); QCOMPARE(productType.value().at(0).quantity,qint64(350));
    const auto dashboard = service.dashboardSummary(QDate(2026,9,1)); QVERIFY(dashboard.ok());
    QCOMPARE(dashboard.value().totalInventory,qint64(470)); QCOMPARE(dashboard.value().companyInventory,qint64(0));
    QCOMPARE(dashboard.value().factoryInventory,qint64(470)); QCOMPARE(dashboard.value().monthlyInbound,qint64(820));
    QCOMPARE(dashboard.value().monthlyOutbound,qint64(350)); QCOMPARE(dashboard.value().recentRecords.size(),5);
}

void InventoryQueriesTest::collectionExecErrorsArePropagated()
{
    struct Case { QString table; std::function<bool(InventoryQueryService &)> fails; };
    const QVector<Case> cases={
        {QStringLiteral("inventory_entries"),[](InventoryQueryService &s){ const auto r=s.balance(1,1); return !r.ok()&&r.error().code==QStringLiteral("database"); }},
        {QStringLiteral("inventory_entries"),[](InventoryQueryService &s){ const auto r=s.batchRemaining(1); return !r.ok()&&r.error().code==QStringLiteral("database"); }},
        {QStringLiteral("inventory_entries"),[](InventoryQueryService &s){ const auto r=s.batchBalances(); return !r.ok()&&r.error().code==QStringLiteral("database"); }},
        {QStringLiteral("locations"),[](InventoryQueryService &s){ const auto r=s.locationBalances(); return !r.ok()&&r.error().code==QStringLiteral("database"); }},
        {QStringLiteral("logistics_documents"),[](InventoryQueryService &s){ const auto r=s.batchTimeline(1); return !r.ok()&&r.error().code==QStringLiteral("database"); }},
        {QStringLiteral("logistics_document_lines"),[](InventoryQueryService &s){ const auto r=s.productTypeShipmentSummary({QDate(2026,9,1),QDate(2026,9,30)}); return !r.ok()&&r.error().code==QStringLiteral("database"); }},
        {QStringLiteral("logistics_documents"),[](InventoryQueryService &s){ const auto r=s.dashboardSummary(QDate(2026,9,1)); return !r.ok()&&r.error().code==QStringLiteral("database"); }}
    };
    for(const auto &item:cases) {
        TestDatabase db; QVERIFY(db.openAndMigrate()); InventoryQueryService service(db.database());
        QVERIFY(db.execute(QStringLiteral("DROP TABLE %1").arg(item.table)));
        QVERIFY(item.fails(service));
    }
}

void InventoryQueriesTest::collectionFetchErrorCheckIsPresent()
{
    QFile source(QStringLiteral(SOURCE_ROOT "/src/services/inventory_query_service.cpp"));
    QVERIFY(source.open(QIODevice::ReadOnly|QIODevice::Text)); const QString text=QString::fromUtf8(source.readAll());
    QCOMPARE(text.count(QStringLiteral("while (q.next())")),8);
    QCOMPARE(text.count(QStringLiteral("if (q.lastError().isValid())")),8);
}

QTEST_MAIN(InventoryQueriesTest)
#include "tst_inventory_queries.moc"
