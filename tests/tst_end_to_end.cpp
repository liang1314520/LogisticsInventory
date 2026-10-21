#include "repositories/document_repository.h"
#include "repositories/inventory_repository.h"
#include "repositories/master_data_repository.h"
#include "services/inbound_service.h"
#include "services/inventory_query_service.h"
#include "services/logistics_service.h"
#include "services/master_data_service.h"
#include "test_support.h"

#include <algorithm>
#include <memory>
#include <QtTest>

class EndToEndTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void completesThe820ItemWorkflow();
    void outboundShortageIsSaved();
    void splitShipmentReturnAndReprocess();
    void allowsOverIssueAcrossModelsAndSupplementalReturns();
    void returnFailureRollsBackAllChanges();
private:
    EntityId location(const QString &name, LocationType type, bool inventory);
    Result<EntityId> saveLogistics(const QString &number, DocumentType type,
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
    EntityId factoryBId=0, externalId=0, batchId=0, personId=0;
};

void EndToEndTest::init()
{
    db=std::make_unique<TestDatabase>(); QVERIFY(db->openAndMigrate());
    masterRepository=std::make_unique<MasterDataRepository>(db->database());
    master=std::make_unique<MasterDataService>(*masterRepository);
    documents=std::make_unique<DocumentRepository>(db->database());
    inventory=std::make_unique<InventoryRepository>(db->database());
    inbound=std::make_unique<InboundService>(db->database(),*masterRepository,*documents,*inventory);
    logistics=std::make_unique<LogisticsService>(db->database(),*masterRepository,*documents,*inventory);
    queries=std::make_unique<InventoryQueryService>(db->database());
    const auto category=master->createCategory(QStringLiteral("设备")); QVERIFY(category.ok());
    const auto product=master->createProductType(category.value().id,QStringLiteral("GW-820"),{});
    QVERIFY(product.ok()); productTypeId=product.value().id;
    companyId=location(QStringLiteral("公司仓"),LocationType::Company,true);
    factoryAId=location(QStringLiteral("工厂 A"),LocationType::Factory,true);
    factoryBId=location(QStringLiteral("工厂 B"),LocationType::Factory,true);
    externalId=location(QStringLiteral("Amazon"),LocationType::Other,false);
    const auto person=master->createPersonnel(QStringLiteral("经办人")); QVERIFY(person.ok());
    personId=person.value().id;
    const auto batch=inbound->create({QStringLiteral("B-820"),productTypeId,820,companyId,
        QDateTime(QDate(2026,9,1),QTime(8,0)),{},QStringLiteral("V40"),personId});
    QVERIFY(batch.ok()); batchId=batch.value();
}

EntityId EndToEndTest::location(const QString &name, LocationType type, bool inventoryFlag)
{
    const auto result=master->createLocation(name,type,inventoryFlag);
    return result.ok()?result.value().id:0;
}

Result<EntityId> EndToEndTest::saveLogistics(const QString &number, DocumentType type,
    EntityId source, EntityId destination, EntityId batch, qint64 quantity)
{
    return logistics->create({number,type,source,destination,QDateTime(QDate(2026,9,2),QTime(8,0)),{},
        {{batch,quantity}},type==DocumentType::Outbound?personId:0});
}

void EndToEndTest::completesThe820ItemWorkflow()
{
    QVERIFY(saveLogistics(QStringLiteral("T-A"),DocumentType::Transfer,companyId,factoryAId,batchId,500).ok());
    QVERIFY(saveLogistics(QStringLiteral("T-B"),DocumentType::Transfer,companyId,factoryBId,batchId,320).ok());
    QVERIFY(saveLogistics(QStringLiteral("O-A"),DocumentType::Outbound,factoryAId,externalId,batchId,250).ok());
    QVERIFY(saveLogistics(QStringLiteral("O-B"),DocumentType::Outbound,factoryBId,externalId,batchId,100).ok());
    QCOMPARE(inventory->quantity(batchId,companyId),qint64(0));
    QCOMPARE(inventory->quantity(batchId,factoryAId),qint64(250));
    QCOMPARE(inventory->quantity(batchId,factoryBId),qint64(220));
    QCOMPARE(inventory->quantity(batchId,externalId),qint64(0));
    QCOMPARE(queries->batchRemaining(batchId).value(),qint64(470));
    const auto dashboard=queries->dashboardSummary(QDate(2026,9,1)); QVERIFY(dashboard.ok());
    QCOMPARE(dashboard.value().totalInventory,qint64(470));
    QCOMPARE(dashboard.value().companyInventory,qint64(0));
    QCOMPARE(dashboard.value().factoryInventory,qint64(470));
}

void EndToEndTest::outboundShortageIsSaved()
{
    const auto saved=saveLogistics(QStringLiteral("O-SUPPLEMENT"),DocumentType::Outbound,
        factoryAId,externalId,batchId,821);
    QVERIFY(saved.ok());
    QCOMPARE(documents->logisticsById(saved.value()).value().status,DocumentStatus::Confirmed);
    QCOMPARE(inventory->quantity(batchId,companyId),qint64(820));
    QCOMPARE(inventory->quantity(batchId,factoryAId),qint64(-821));
    QCOMPARE(queries->batchRemaining(batchId).value(),qint64(-1));
    QCOMPARE(queries->negativeBalanceCount().value(),qint64(1));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Logistics"),saved.value()).size(),1);
}

void EndToEndTest::splitShipmentReturnAndReprocess()
{
    const auto f25 = master->createProductModel(productTypeId, QStringLiteral("F25"), {});
    const auto p25 = master->createProductModel(productTypeId, QStringLiteral("P25"), {});
    QVERIFY(f25.ok() && p25.ok());
    const QDateTime at(QDate(2026, 9, 2), QTime(8, 0));
    QVERIFY(saveLogistics(QStringLiteral("T-SPLIT"),DocumentType::Transfer,companyId,factoryAId,batchId,820).ok());
    LogisticsInput sentInput{QStringLiteral("O-SPLIT"), DocumentType::Outbound,
        factoryAId, externalId, at, {}, {{batchId, 500, f25.value().id}, {batchId, 320, p25.value().id}}, personId};
    const auto sent = logistics->create(sentInput);
    QVERIFY(sent.ok());
    QCOMPARE(queries->batchRemaining(batchId).value(), qint64(0));
    LogisticsInput returnInput{QStringLiteral("R-50"), DocumentType::Return,
        factoryAId, companyId, at.addDays(1), {}, {{batchId, 50, f25.value().id}}};
    const auto received = logistics->create(returnInput);
    QVERIFY(received.ok());
    QVERIFY(master->updateProductModel(f25.value().id, productTypeId, QStringLiteral("F25-NEW"), {}).ok());
    QVERIFY(master->setProductModelEnabled(f25.value().id, false).ok());
    QCOMPARE(queries->batchRemaining(batchId).value(), qint64(0));
    QCOMPARE(documents->logisticsById(received.value()).value().lines.front().modelCodeSnapshot, QStringLiteral("F25"));
    QCOMPARE(inventory->quantity(batchId, externalId), qint64(0));
    QVERIFY(logistics->cancel(sent.value()).ok());
    QVERIFY(logistics->update(sent.value(), sentInput).ok());
    const auto resent = logistics->create({QStringLiteral("O-REWORK"), DocumentType::Outbound,
        factoryAId, externalId, at.addDays(2), {}, {{batchId, 30, p25.value().id}}, personId});
    QVERIFY(resent.ok());
    QCOMPARE(queries->batchRemaining(batchId).value(), qint64(-30));
    QVERIFY(logistics->cancel(received.value()).ok());
    QVERIFY(logistics->update(received.value(), returnInput).ok());
    const auto summary = queries->productTypeShipmentSummary({QDate(2026,9,1),QDate(2026,9,30)});
    QVERIFY(summary.ok());
    QCOMPARE(summary.value().size(), 2);
    QCOMPARE(summary.value().at(0).modelCode, QStringLiteral("F25-NEW"));
    QCOMPARE(summary.value().at(0).quantity, qint64(500));
    QCOMPARE(summary.value().at(0).returnedQuantity, qint64(50));
    QCOMPARE(summary.value().at(1).quantity, qint64(350));
    const auto dashboard = queries->dashboardSummary(QDate(2026,9,1));
    QVERIFY(dashboard.ok());
    QCOMPARE(dashboard.value().totalInventory, qint64(-30));
    QCOMPARE(dashboard.value().monthlyOutbound, qint64(850));
    QCOMPARE(dashboard.value().monthlyReturned, qint64(50));
    const auto timeline = queries->batchTimeline(batchId);
    QVERIFY(timeline.ok());
    QVERIFY(std::any_of(timeline.value().cbegin(), timeline.value().cend(), [](const BatchTimelineItem &item) {
        return item.direction == QStringLiteral("退回接收") && item.modelCode == QStringLiteral("F25-NEW");
    }));
    QVERIFY(logistics->cancel(resent.value()).ok());
    QVERIFY(logistics->cancel(received.value()).ok());
    QCOMPARE(queries->batchRemaining(batchId).value(), qint64(0));
    QVERIFY(logistics->update(received.value(), returnInput).ok());
    QCOMPARE(documents->logisticsById(received.value()).value().lines.front().productModelId, f25.value().id);
    QVERIFY(logistics->cancel(sent.value()).ok());
    QCOMPARE(queries->batchRemaining(batchId).value(), qint64(820));
}

void EndToEndTest::allowsOverIssueAcrossModelsAndSupplementalReturns()
{
    const auto f25 = master->createProductModel(productTypeId, QStringLiteral("F25"), {});
    const auto p25 = master->createProductModel(productTypeId, QStringLiteral("P25"), {});
    QVERIFY(f25.ok() && p25.ok());
    const QDateTime at(QDate(2026,9,2),QTime(8,0));
    LogisticsInput value{QStringLiteral("O-OVER"),DocumentType::Outbound,factoryAId,externalId,
        at,{},{{batchId,500,f25.value().id},{batchId,321,p25.value().id}},personId};
    const auto sent=logistics->create(value);
    QVERIFY(sent.ok());
    QCOMPARE(queries->batchRemaining(batchId).value(),qint64(-1));
    value.lines.back().quantity=320;
    QVERIFY(logistics->update(sent.value(),value).ok());
    QCOMPARE(queries->batchRemaining(batchId).value(),qint64(0));
    const auto first=logistics->create({QStringLiteral("R-FIRST"),DocumentType::Return,
        factoryAId,companyId,at,{},{{batchId,490,f25.value().id}}});
    const auto second=logistics->create({QStringLiteral("R-SUPPLEMENT"),DocumentType::Return,
        factoryAId,companyId,at,{},{{batchId,11,f25.value().id}}});
    QVERIFY(first.ok() && second.ok());
    QCOMPARE(queries->batchRemaining(batchId).value(),qint64(0));
    const EntityId stranger=location(QStringLiteral("外部地点"),LocationType::Customer,false);
    const auto wrongSource=logistics->create({QStringLiteral("R-WRONG"),DocumentType::Return,
        stranger,companyId,at,{},{{batchId,1,f25.value().id}}});
    QVERIFY(!wrongSource.ok());
    QCOMPARE(wrongSource.error().field,QStringLiteral("sourceLocationId"));
    const auto category=master->createCategory(QStringLiteral("其他产品")); QVERIFY(category.ok());
    const auto type=master->createProductType(category.value().id,QStringLiteral("OTHER"),{}); QVERIFY(type.ok());
    const auto model=master->createProductModel(type.value().id,QStringLiteral("F25"),{}); QVERIFY(model.ok());
    const auto invalid=logistics->create({QStringLiteral("O-WRONG"),DocumentType::Outbound,
        factoryAId,externalId,at,{},{{batchId,1,model.value().id}},personId});
    QVERIFY(!invalid.ok());
    QCOMPARE(invalid.error().field,QStringLiteral("lines[0].productModelId"));
}

void EndToEndTest::returnFailureRollsBackAllChanges()
{
    QVERIFY(saveLogistics(QStringLiteral("O-RAW"),DocumentType::Outbound,factoryAId,externalId,batchId,820).ok());
    const auto received=saveLogistics(QStringLiteral("R-RAW"),DocumentType::Return,factoryAId,companyId,batchId,50);
    QVERIFY(received.ok());
    QVERIFY(db->execute(QStringLiteral(
        "CREATE TRIGGER abort_return BEFORE UPDATE OF status ON logistics_documents "
        "WHEN NEW.type='Return' AND NEW.status='Confirmed' "
        "BEGIN SELECT RAISE(ABORT,'forced return failure'); END")));
    LogisticsInput changed{QStringLiteral("R-RAW"),DocumentType::Return,factoryAId,companyId,
        QDateTime(QDate(2026,9,2),QTime(8,0)),{},{{batchId,60}}};
    const auto result=logistics->update(received.value(),changed);
    QVERIFY(!result.ok());
    QCOMPARE(result.error().code,QStringLiteral("database"));
    QCOMPARE(documents->logisticsById(received.value()).value().lines.front().quantity,qint64(50));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Logistics"),received.value()).size(),2);
    QCOMPARE(queries->batchRemaining(batchId).value(),qint64(0));
    QCOMPARE(queries->balance(batchId,factoryAId).value(),qint64(-870));
}

QTEST_MAIN(EndToEndTest)
#include "tst_end_to_end.moc"
