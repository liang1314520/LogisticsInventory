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
    void splitShipmentReturnAndReprocess();
    void rejectsOverIssueAcrossModelsAndExcessReturns();
    void returnFailureRollsBackAllChanges();
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

void EndToEndTest::splitShipmentReturnAndReprocess()
{
    const auto f25 = master->createProductModel(productTypeId, QStringLiteral("F25"), {});
    const auto p25 = master->createProductModel(productTypeId, QStringLiteral("P25"), {});
    QVERIFY(f25.ok() && p25.ok());
    const QDateTime at(QDate(2026, 9, 2), QTime(8, 0));
    const auto sent = logistics->createDraft({QStringLiteral("O-SPLIT"), DocumentType::Outbound,
        companyId, externalId, at, {}, {{batchId, 500, f25.value().id}, {batchId, 320, p25.value().id}}});
    QVERIFY(sent.ok());
    QVERIFY(logistics->confirm(sent.value()).ok());
    QCOMPARE(queries->batchRemaining(batchId).value(), qint64(0));
    QCOMPARE(documents->logisticsById(sent.value()).value().lines.front().productModelId, f25.value().id);

    QVERIFY(master->updateProductModel(f25.value().id, productTypeId, QStringLiteral("F25-NEW"), {}).ok());
    QVERIFY(master->setProductModelEnabled(f25.value().id, false).ok());
    const auto received = logistics->createDraft({QStringLiteral("R-50"), DocumentType::Return,
        externalId, companyId, at.addDays(1), {}, {{batchId, 50, f25.value().id}}});
    QVERIFY(received.ok());
    QCOMPARE(queries->batchRemaining(batchId).value(), qint64(0));
    QVERIFY(logistics->confirm(received.value()).ok());
    QCOMPARE(queries->batchRemaining(batchId).value(), qint64(50));
    const auto saved = documents->logisticsById(received.value());
    QVERIFY(saved.ok());
    QCOMPARE(saved.value().type, DocumentType::Return);
    QCOMPARE(saved.value().lines.front().modelCodeSnapshot, QStringLiteral("F25"));
    QCOMPARE(inventory->quantity(batchId, externalId), qint64(0));
    QCOMPARE(logistics->cancel(sent.value()).error().code, QStringLiteral("return_dependency"));

    const auto resent = logistics->createDraft({QStringLiteral("O-REWORK"), DocumentType::Outbound,
        companyId, externalId, at.addDays(2), {}, {{batchId, 30, p25.value().id}}});
    QVERIFY(resent.ok());
    QVERIFY(logistics->confirm(resent.value()).ok());
    QCOMPARE(queries->batchRemaining(batchId).value(), qint64(20));
    QCOMPARE(logistics->cancel(received.value()).error().code, QStringLiteral("stock_dependency"));

    const auto summary = queries->productTypeShipmentSummary({QDate(2026, 9, 1), QDate(2026, 9, 30)});
    QVERIFY(summary.ok());
    QCOMPARE(summary.value().size(), 2);
    QCOMPARE(summary.value().at(0).modelCode, QStringLiteral("F25"));
    QCOMPARE(summary.value().at(0).quantity, qint64(500));
    QCOMPARE(summary.value().at(0).returnedQuantity, qint64(50));
    QCOMPARE(summary.value().at(1).quantity, qint64(350));
    const auto dashboard = queries->dashboardSummary(QDate(2026, 9, 1));
    QVERIFY(dashboard.ok());
    QCOMPARE(dashboard.value().totalInventory, qint64(20));
    QCOMPARE(dashboard.value().monthlyOutbound, qint64(850));
    QCOMPARE(dashboard.value().monthlyReturned, qint64(50));
    const auto timeline = queries->batchTimeline(batchId);
    QVERIFY(timeline.ok());
    QCOMPARE(timeline.value().at(3).direction, QStringLiteral("退回入库"));
    QCOMPARE(timeline.value().at(3).modelCode, QStringLiteral("F25"));

    QVERIFY(logistics->cancel(resent.value()).ok());
    QVERIFY(logistics->cancel(received.value()).ok());
    QCOMPARE(queries->batchRemaining(batchId).value(), qint64(0));
    const auto copy = logistics->copyToDraft(received.value());
    QVERIFY(copy.ok());
    QCOMPARE(documents->logisticsById(copy.value()).value().lines.front().productModelId, f25.value().id);
    QVERIFY(logistics->cancel(sent.value()).ok());
    QCOMPARE(queries->batchRemaining(batchId).value(), qint64(820));
}

void EndToEndTest::rejectsOverIssueAcrossModelsAndExcessReturns()
{
    const auto f25 = master->createProductModel(productTypeId, QStringLiteral("F25"), {});
    const auto p25 = master->createProductModel(productTypeId, QStringLiteral("P25"), {});
    QVERIFY(f25.ok() && p25.ok());
    const QDateTime at(QDate(2026, 9, 2), QTime(8, 0));
    LogisticsDraft value{QStringLiteral("O-OVER"), DocumentType::Outbound, companyId, externalId,
        at, {}, {{batchId, 500, f25.value().id}, {batchId, 321, p25.value().id}}};
    const auto sent = logistics->createDraft(value);
    QVERIFY(sent.ok());
    QCOMPARE(logistics->confirm(sent.value()).error().code, QStringLiteral("insufficient_stock"));
    QVERIFY(inventory->entriesForSource(QStringLiteral("Logistics"), sent.value()).isEmpty());
    value.lines.back().quantity = 320;
    QVERIFY(logistics->updateDraft(sent.value(), value).ok());
    QVERIFY(logistics->confirm(sent.value()).ok());

    const auto first = logistics->createDraft({QStringLiteral("R-FIRST"), DocumentType::Return,
        externalId, companyId, at, {}, {{batchId, 490, f25.value().id}}});
    const auto second = logistics->createDraft({QStringLiteral("R-EXCESS"), DocumentType::Return,
        externalId, companyId, at, {}, {{batchId, 11, f25.value().id}}});
    QVERIFY(first.ok() && second.ok());
    QVERIFY(logistics->confirm(first.value()).ok());
    QCOMPARE(logistics->confirm(second.value()).error().code, QStringLiteral("excess_return"));
    QVERIFY(inventory->entriesForSource(QStringLiteral("Logistics"), second.value()).isEmpty());
    QCOMPARE(queries->batchRemaining(batchId).value(), qint64(490));

    const EntityId stranger = location(QStringLiteral("另一个客户"), LocationType::Customer, false);
    const auto wrongSource = logistics->createDraft({QStringLiteral("R-WRONG"), DocumentType::Return,
        stranger, companyId, at, {}, {{batchId, 1, f25.value().id}}});
    QVERIFY(wrongSource.ok());
    QCOMPARE(logistics->confirm(wrongSource.value()).error().code, QStringLiteral("excess_return"));
    const auto category = master->createCategory(QStringLiteral("其他产品"));
    QVERIFY(category.ok());
    const auto otherType = master->createProductType(category.value().id, QStringLiteral("OTHER"), {});
    QVERIFY(otherType.ok());
    const auto wrongModel = master->createProductModel(otherType.value().id, QStringLiteral("F25"), {});
    QVERIFY(wrongModel.ok());
    const auto invalid = logistics->createDraft({QStringLiteral("O-WRONG"), DocumentType::Outbound,
        companyId, externalId, at, {}, {{batchId, 1, wrongModel.value().id}}});
    QVERIFY(invalid.ok());
    QCOMPARE(logistics->confirm(invalid.value()).error().field, QStringLiteral("lines[0].productModelId"));
}

void EndToEndTest::returnFailureRollsBackAllChanges()
{
    const auto sent = logisticsDraft(QStringLiteral("O-RAW"), DocumentType::Outbound,
                                     companyId, externalId, batchId, 820);
    QVERIFY(sent.ok());
    QVERIFY(logistics->confirm(sent.value()).ok());
    const auto received = logisticsDraft(QStringLiteral("R-RAW"), DocumentType::Return,
                                         externalId, companyId, batchId, 50);
    QVERIFY(received.ok());
    QVERIFY(db->execute(QStringLiteral(
        "CREATE TRIGGER abort_return BEFORE UPDATE OF status ON logistics_documents "
        "WHEN NEW.type='Return' AND NEW.status='Confirmed' "
        "BEGIN SELECT RAISE(ABORT,'forced return failure'); END")));
    QCOMPARE(logistics->confirm(received.value()).error().code, QStringLiteral("database"));
    QCOMPARE(documents->logisticsById(received.value()).value().status, DocumentStatus::Draft);
    QVERIFY(inventory->entriesForSource(QStringLiteral("Logistics"), received.value()).isEmpty());
    QCOMPARE(queries->batchRemaining(batchId).value(), qint64(0));
    QCOMPARE(logistics->returnableLines(externalId).value().front().quantity, qint64(820));
}

QTEST_MAIN(EndToEndTest)
#include "tst_end_to_end.moc"
