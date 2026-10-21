#include "repositories/document_repository.h"
#include "repositories/inventory_repository.h"
#include "repositories/master_data_repository.h"
#include "services/batch_workflow_service.h"
#include "services/inbound_service.h"
#include "services/inventory_query_service.h"
#include "services/logistics_service.h"
#include "services/master_data_service.h"
#include "test_support.h"

#include <memory>
#include <QtTest>

class BatchWorkflowServiceTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void savedInboundAllowsOutboundSupplement();
    void confirmedInboundOffersOutboundOrTransfer();
    void confirmedTransferShowsDistributedStockAndOutboundNext();
    void outboundThenReturnOffersReprocess();
    void zeroBalanceHistoryRemainsTrackableAndCompleted();
    void multipleBatchesHaveIndependentStates();
    void locationQueryFailureIsReturned();
    void negativeLocationDoesNotCompleteWorkflow();
    void cancelledOutboundReopensAvailableStock();
    void partialOutboundStillAllowsReturn();
    void savedRecordsAllowNewActions_data();
    void savedRecordsAllowNewActions();
private:
    EntityId createConfirmedBatch(const QString &batchNo, qint64 quantity);
    Result<EntityId> createLogistics(const QString &number, DocumentType type,
                                     EntityId source, EntityId destination,
                                     EntityId batch, qint64 quantity, EntityId model = 0);
    std::unique_ptr<TestDatabase> db;
    std::unique_ptr<MasterDataRepository> masterRepository;
    std::unique_ptr<MasterDataService> master;
    std::unique_ptr<DocumentRepository> documents;
    std::unique_ptr<InventoryRepository> inventory;
    std::unique_ptr<InboundService> inbound;
    std::unique_ptr<LogisticsService> logistics;
    std::unique_ptr<InventoryQueryService> queries;
    std::unique_ptr<BatchWorkflowService> workflow;
    EntityId productTypeId = 0, companyId = 0, factoryId = 0, externalId = 0, modelId = 0, personId = 0;
};

void BatchWorkflowServiceTest::init()
{
    db = std::make_unique<TestDatabase>();
    QVERIFY(db->openAndMigrate());
    masterRepository = std::make_unique<MasterDataRepository>(db->database());
    master = std::make_unique<MasterDataService>(*masterRepository);
    documents = std::make_unique<DocumentRepository>(db->database());
    inventory = std::make_unique<InventoryRepository>(db->database());
    inbound = std::make_unique<InboundService>(db->database(), *masterRepository, *documents, *inventory);
    logistics = std::make_unique<LogisticsService>(db->database(), *masterRepository, *documents, *inventory);
    queries = std::make_unique<InventoryQueryService>(db->database());
    const auto category = master->createCategory(QStringLiteral("设备"));
    QVERIFY(category.ok());
    const auto type = master->createProductType(category.value().id, QStringLiteral("保险箱"), {});
    QVERIFY(type.ok());
    productTypeId = type.value().id;
    const auto model = master->createProductModel(productTypeId, QStringLiteral("F25"), {});
    const auto company = master->createLocation(QStringLiteral("公司仓"), LocationType::Company, true);
    const auto factory = master->createLocation(QStringLiteral("工厂A"), LocationType::Factory, true);
    const auto external = master->createLocation(QStringLiteral("Amazon"), LocationType::Other, false);
    const auto person = master->createPersonnel(QStringLiteral("经办人"));
    QVERIFY(model.ok() && company.ok() && factory.ok() && external.ok() && person.ok());
    modelId = model.value().id;
    companyId = company.value().id;
    factoryId = factory.value().id;
    externalId = external.value().id;
    personId = person.value().id;
    workflow = std::make_unique<BatchWorkflowService>(*master, *documents, *inventory, *queries);
}

EntityId BatchWorkflowServiceTest::createConfirmedBatch(const QString &batchNo, qint64 quantity)
{
    const auto created = inbound->create({batchNo, productTypeId, quantity, companyId,
        QDateTime::currentDateTime(), {}, QStringLiteral("V40"), personId});
    return created.ok() ? created.value() : 0;
}

Result<EntityId> BatchWorkflowServiceTest::createLogistics(
    const QString &number, DocumentType type, EntityId source, EntityId destination,
    EntityId batch, qint64 quantity, EntityId model)
{
    return logistics->create({number, type, source, destination, QDateTime::currentDateTime(), {},
        {{batch, quantity, model}}, type == DocumentType::Outbound ? personId : 0});
}

void BatchWorkflowServiceTest::savedInboundAllowsOutboundSupplement()
{
    const EntityId batch = createConfirmedBatch(QStringLiteral("B-SAVED"), 10);
    QVERIFY(batch > 0);
    const auto state = workflow->state(batch);
    QVERIFY(state.ok());
    QCOMPARE(state.value().steps.at(1).status, WorkflowStepStatus::Completed);
    QVERIFY(state.value().availableActions.contains(WorkflowAction::CreateOutbound));
    QVERIFY(workflow->canStartAction(batch, WorkflowAction::CreateOutbound).ok());
    QCOMPARE(state.value().nextPage, 2);
}

void BatchWorkflowServiceTest::confirmedInboundOffersOutboundOrTransfer()
{
    const EntityId batch = createConfirmedBatch(QStringLiteral("B-CONFIRMED"), 10);
    QVERIFY(batch > 0);
    const auto state = workflow->state(batch);
    QVERIFY(state.ok());
    QCOMPARE(state.value().steps.at(1).status, WorkflowStepStatus::Completed);
    QVERIFY(state.value().availableActions.contains(WorkflowAction::CreateOutbound));
    QVERIFY(state.value().availableActions.contains(WorkflowAction::CreateTransfer));
    QCOMPARE(state.value().nextPage, 2);
}

void BatchWorkflowServiceTest::confirmedTransferShowsDistributedStockAndOutboundNext()
{
    const EntityId batch = createConfirmedBatch(QStringLiteral("B-TRANSFER"), 10);
    QVERIFY(batch > 0);
    QVERIFY(createLogistics(QStringLiteral("T-1"), DocumentType::Transfer, companyId, factoryId, batch, 5).ok());
    const auto state = workflow->state(batch);
    QVERIFY(state.ok());
    QCOMPARE(state.value().currentLocations.size(), 2);
    QCOMPARE(state.value().steps.at(2).status, WorkflowStepStatus::Completed);
    QVERIFY(state.value().availableActions.contains(WorkflowAction::CreateOutbound));
    QCOMPARE(state.value().nextPage, 3);
}

void BatchWorkflowServiceTest::outboundThenReturnOffersReprocess()
{
    const EntityId batch = createConfirmedBatch(QStringLiteral("B-RETURN"), 10);
    QVERIFY(batch > 0);
    QVERIFY(createLogistics(QStringLiteral("T-1"), DocumentType::Transfer, companyId, factoryId, batch, 10).ok());
    QVERIFY(createLogistics(QStringLiteral("O-1"), DocumentType::Outbound, factoryId, externalId, batch, 7, modelId).ok());
    QVERIFY(createLogistics(QStringLiteral("R-1"), DocumentType::Return, factoryId, companyId, batch, 3, modelId).ok());
    const auto state = workflow->state(batch);
    QVERIFY(state.ok());
    QCOMPARE(state.value().currentQuantity, qint64(3));
    QVERIFY(state.value().availableActions.contains(WorkflowAction::CreateOutbound));
    QCOMPARE(state.value().nextPage, 2);
}

void BatchWorkflowServiceTest::zeroBalanceHistoryRemainsTrackableAndCompleted()
{
    const EntityId batch = createConfirmedBatch(QStringLiteral("B-ZERO"), 10);
    QVERIFY(batch > 0);
    QVERIFY(createLogistics(QStringLiteral("T-ZERO"), DocumentType::Transfer, companyId, factoryId, batch, 10).ok());
    QVERIFY(createLogistics(QStringLiteral("O-ZERO"), DocumentType::Outbound, factoryId, externalId, batch, 10, modelId).ok());
    const auto state = workflow->state(batch);
    QVERIFY(state.ok());
    QCOMPARE(state.value().currentQuantity, qint64(0));
    QCOMPARE(state.value().steps.at(5).status, WorkflowStepStatus::Completed);
    QVERIFY(state.value().availableActions.contains(WorkflowAction::ViewTracking));
}

void BatchWorkflowServiceTest::negativeLocationDoesNotCompleteWorkflow()
{
    const EntityId batch = createConfirmedBatch(QStringLiteral("B-SUPPLEMENT"), 10);
    QVERIFY(batch > 0);
    QVERIFY(createLogistics(QStringLiteral("O-SUPPLEMENT"), DocumentType::Outbound,
        factoryId, externalId, batch, 10, modelId).ok());
    const auto state = workflow->state(batch);
    QVERIFY(state.ok());
    QCOMPARE(state.value().currentQuantity, qint64(0));
    QCOMPARE(state.value().steps.at(5).status, WorkflowStepStatus::NeedsAction);
    QCOMPARE(state.value().overallStatus, QStringLiteral("需核对"));
    QVERIFY(workflow->canStartAction(batch, WorkflowAction::CreateTransfer).ok());
}

void BatchWorkflowServiceTest::cancelledOutboundReopensAvailableStock()
{
    const EntityId batch = createConfirmedBatch(QStringLiteral("B-CANCEL-OUT"), 10);
    QVERIFY(batch > 0);
    QVERIFY(createLogistics(QStringLiteral("T-CANCEL-OUT"), DocumentType::Transfer, companyId, factoryId, batch, 10).ok());
    const auto outbound = createLogistics(QStringLiteral("O-CANCEL-OUT"), DocumentType::Outbound,
        factoryId, externalId, batch, 10, modelId);
    QVERIFY(outbound.ok());
    QVERIFY(logistics->cancel(outbound.value()).ok());
    const auto state = workflow->state(batch);
    QVERIFY(state.ok());
    QCOMPARE(state.value().currentQuantity, qint64(10));
    QVERIFY(state.value().availableActions.contains(WorkflowAction::CreateOutbound));
    QCOMPARE(state.value().nextPage, 3);
}

void BatchWorkflowServiceTest::locationQueryFailureIsReturned()
{
    const EntityId batch = createConfirmedBatch(QStringLiteral("B-QUERY-ERROR"), 10);
    QVERIFY(batch > 0);
    QVERIFY(db->execute(QStringLiteral("DROP TABLE inventory_entries")));
    const auto state = workflow->state(batch);
    QVERIFY(!state.ok());
    QCOMPARE(state.error().code, QStringLiteral("database"));
}

void BatchWorkflowServiceTest::multipleBatchesHaveIndependentStates()
{
    const EntityId first = createConfirmedBatch(QStringLiteral("B-FIRST"), 10);
    const EntityId second = createConfirmedBatch(QStringLiteral("B-SECOND"), 20);
    QVERIFY(first > 0 && second > 0);
    QVERIFY(createLogistics(QStringLiteral("T-FIRST"), DocumentType::Transfer, companyId, factoryId, first, 4).ok());
    const auto firstState = workflow->state(first);
    const auto secondState = workflow->state(second);
    QVERIFY(firstState.ok() && secondState.ok());
    QCOMPARE(firstState.value().batchId, first);
    QCOMPARE(secondState.value().batchId, second);
    QCOMPARE(firstState.value().currentQuantity, qint64(10));
    QCOMPARE(secondState.value().currentQuantity, qint64(20));
    QCOMPARE(firstState.value().currentLocations.size(), 2);
    QCOMPARE(secondState.value().currentLocations.size(), 1);
}

void BatchWorkflowServiceTest::partialOutboundStillAllowsReturn()
{
    const EntityId batch = createConfirmedBatch(QStringLiteral("B-PARTIAL"), 10);
    QVERIFY(batch > 0);
    QVERIFY(createLogistics(QStringLiteral("O-PARTIAL"), DocumentType::Outbound, factoryId, externalId, batch, 6, modelId).ok());
    const auto state = workflow->state(batch);
    QVERIFY(state.ok());
    QCOMPARE(state.value().currentQuantity, qint64(4));
    QVERIFY(workflow->canStartAction(batch, WorkflowAction::CreateReturn).ok());
    QVERIFY(workflow->canStartAction(batch, WorkflowAction::CreateOutbound).ok());
}

void BatchWorkflowServiceTest::savedRecordsAllowNewActions_data()
{
    QTest::addColumn<DocumentType>("type");
    QTest::newRow("transfer") << DocumentType::Transfer;
    QTest::newRow("outbound") << DocumentType::Outbound;
    QTest::newRow("return") << DocumentType::Return;
}

void BatchWorkflowServiceTest::savedRecordsAllowNewActions()
{
    QFETCH(DocumentType, type);
    const EntityId batch = createConfirmedBatch(QStringLiteral("B-RECORD"), 10);
    QVERIFY(batch > 0);
    const auto saved = createLogistics(QStringLiteral("L-RECORD"), type,
        type == DocumentType::Transfer ? companyId : factoryId,
        type == DocumentType::Transfer ? factoryId : type == DocumentType::Return ? companyId : externalId,
        batch, 1);
    QVERIFY(saved.ok());
    for (const auto action : {WorkflowAction::CreateTransfer, WorkflowAction::CreateOutbound, WorkflowAction::CreateReturn})
        QVERIFY(workflow->canStartAction(batch, action).ok());
}

QTEST_MAIN(BatchWorkflowServiceTest)
#include "tst_batch_workflow_service.moc"
