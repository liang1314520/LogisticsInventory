#include "repositories/document_repository.h"
#include "repositories/inventory_repository.h"
#include "repositories/master_data_repository.h"
#include "services/inbound_service.h"
#include "services/inventory_query_service.h"
#include "services/logistics_service.h"
#include "services/master_data_service.h"
#include "test_support.h"

#include <memory>
#include <QtTest>

class EndToEndTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void completesThe820ItemWorkflow();
    void failedOutboundIsAtomic();
private:
    EntityId location(const QString &name, LocationType type, bool inventory);
    Result<EntityId> logisticsDraft(const QString &number, DocumentType type,
                                    EntityId source, EntityId destination,
                                    EntityId batch, qint64 quantity);

    std::unique_ptr<TestDatabase> db;
    std::unique_ptr<MasterDataRepository> masterRepository;
    std::unique_ptr<MasterDataService> master;
    std::unique_ptr<DocumentRepository> documents;
    std::unique_ptr<InventoryRepository> inventory;
    std::unique_ptr<InboundService> inbound;
    std::unique_ptr<LogisticsService> logistics;
    std::unique_ptr<InventoryQueryService> queries;
    EntityId productTypeId=0, companyId=0, factoryAId=0;
    EntityId factoryBId=0, externalId=0, batchId=0;
};

void EndToEndTest::init()
{
    db=std::make_unique<TestDatabase>();QVERIFY(db->openAndMigrate());
    masterRepository=std::make_unique<MasterDataRepository>(db->database());
    master=std::make_unique<MasterDataService>(*masterRepository);
    documents=std::make_unique<DocumentRepository>(db->database());
    inventory=std::make_unique<InventoryRepository>(db->database());
    inbound=std::make_unique<InboundService>(db->database(),*masterRepository,*documents,*inventory);
    logistics=std::make_unique<LogisticsService>(db->database(),*masterRepository,*documents,*inventory);
    queries=std::make_unique<InventoryQueryService>(db->database());
    const auto category=master->createCategory(QStringLiteral("设备"));QVERIFY(category.ok());
    const auto productType=master->createProductType(category.value().id,QStringLiteral("GW-820"),{});QVERIFY(productType.ok());productTypeId=productType.value().id;
    companyId=location(QStringLiteral("公司仓"),LocationType::Company,true);
    factoryAId=location(QStringLiteral("工厂 A"),LocationType::Factory,true);
    factoryBId=location(QStringLiteral("工厂 B"),LocationType::Factory,true);
    externalId=location(QStringLiteral("客户现场"),LocationType::Customer,false);
    const auto batch=inbound->createDraft({QStringLiteral("B-820"),productTypeId,820,companyId,QDateTime(QDate(2026,9,1),QTime(8,0)),{}});QVERIFY(batch.ok());batchId=batch.value();QVERIFY(inbound->confirm(batchId).ok());
}

EntityId EndToEndTest::location(const QString &name, LocationType type, bool inventoryFlag)
{
    const auto result=master->createLocation(name,type,inventoryFlag);return result.ok()?result.value().id:0;
}

Result<EntityId> EndToEndTest::logisticsDraft(const QString &number,DocumentType type,
    EntityId source,EntityId destination,EntityId batch,qint64 quantity)
{
    return logistics->createDraft({number,type,source,destination,QDateTime(QDate(2026,9,2),QTime(8,0)),{},{{batch,quantity}}});
}

void EndToEndTest::completesThe820ItemWorkflow()
{
    const auto toA=logisticsDraft(QStringLiteral("T-A"),DocumentType::Transfer,companyId,factoryAId,batchId,500);QVERIFY(toA.ok());QVERIFY(logistics->confirm(toA.value()).ok());
    const auto toB=logisticsDraft(QStringLiteral("T-B"),DocumentType::Transfer,companyId,factoryBId,batchId,320);QVERIFY(toB.ok());QVERIFY(logistics->confirm(toB.value()).ok());
    const auto outA=logisticsDraft(QStringLiteral("O-A"),DocumentType::Outbound,factoryAId,externalId,batchId,250);QVERIFY(outA.ok());QVERIFY(logistics->confirm(outA.value()).ok());
    const auto outB=logisticsDraft(QStringLiteral("O-B"),DocumentType::Outbound,factoryBId,externalId,batchId,100);QVERIFY(outB.ok());QVERIFY(logistics->confirm(outB.value()).ok());
    QCOMPARE(inventory->quantity(batchId,companyId),qint64(0));
    QCOMPARE(inventory->quantity(batchId,factoryAId),qint64(250));
    QCOMPARE(inventory->quantity(batchId,factoryBId),qint64(220));
    QCOMPARE(inventory->quantity(batchId,externalId),qint64(0));
    const auto remaining=queries->batchRemaining(batchId);QVERIFY(remaining.ok());QCOMPARE(remaining.value(),qint64(470));
    const auto dashboard=queries->dashboardSummary(QDate(2026,9,1));QVERIFY(dashboard.ok());QCOMPARE(dashboard.value().totalInventory,qint64(470));QCOMPARE(dashboard.value().companyInventory,qint64(0));QCOMPARE(dashboard.value().factoryInventory,qint64(470));
}

void EndToEndTest::failedOutboundIsAtomic()
{
    const auto failed=logisticsDraft(QStringLiteral("O-FAIL"),DocumentType::Outbound,companyId,externalId,batchId,821);QVERIFY(failed.ok());
    const auto result=logistics->confirm(failed.value());QVERIFY(!result.ok());QCOMPARE(result.error().code,QStringLiteral("insufficient_stock"));
    const auto saved=documents->logisticsById(failed.value());QVERIFY(saved.ok());QCOMPARE(saved.value().status,DocumentStatus::Draft);
    QCOMPARE(inventory->quantity(batchId,companyId),qint64(820));
    const auto entries=inventory->entriesForSourceResult(QStringLiteral("Logistics"),failed.value());QVERIFY(entries.ok());QVERIFY(entries.value().isEmpty());
}

QTEST_MAIN(EndToEndTest)
#include "tst_end_to_end.moc"
