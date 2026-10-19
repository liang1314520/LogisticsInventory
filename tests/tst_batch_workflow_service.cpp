#include "repositories/document_repository.h"
#include "repositories/inventory_repository.h"
#include "repositories/master_data_repository.h"
#include "services/batch_workflow_service.h"
#include "services/inbound_service.h"
#include "services/inventory_query_service.h"
#include "services/logistics_service.h"
#include "services/master_data_service.h"
#include "test_support.h"

#include <algorithm>
#include <memory>
#include <QtTest>

class BatchWorkflowServiceTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void draftInboundIsInProgressAndBlocksOutbound();
    void confirmedInboundOffersDirectOutboundOrTransfer();
    void confirmedTransferShowsDistributedStockAndOutboundNext();
    void outboundThenReturnOffersReprocess();
    void zeroBalanceHistoryRemainsTrackableAndCompleted();
    void multipleBatchesHaveIndependentStates();
    void locationQueryFailureIsReturned();
    void directOutboundCompletesWorkflow();
    void cancelledOutboundReopensAvailableStock();
    void partialOutboundStillAllowsReturn();
    void pendingDraftBlocksNewActions_data();
    void pendingDraftBlocksNewActions();

private:
    EntityId addLocation(const QString &name, LocationType type, bool countsAsInventory);
    EntityId createConfirmedBatch(const QString &batchNo, qint64 quantity);
    Result<EntityId> createLogistics(const QString &number, DocumentType type,
                                     EntityId source, EntityId destination,
                                     EntityId batch, qint64 quantity,
                                     EntityId model = 0);
    bool containsAction(const QVector<WorkflowAction> &actions, WorkflowAction action) const;

    std::unique_ptr<TestDatabase> db;
    std::unique_ptr<MasterDataRepository> masterRepository;
    std::unique_ptr<MasterDataService> master;
    std::unique_ptr<DocumentRepository> documents;
    std::unique_ptr<InventoryRepository> inventory;
    std::unique_ptr<InboundService> inbound;
    std::unique_ptr<LogisticsService> logistics;
    std::unique_ptr<InventoryQueryService> queries;
    std::unique_ptr<BatchWorkflowService> workflow;
    EntityId productTypeId = 0;
    EntityId companyId = 0;
    EntityId factoryId = 0;
    EntityId externalId = 0;
    EntityId modelId = 0;
};

void BatchWorkflowServiceTest::init()
{
    db = std::make_unique<TestDatabase>();
    QVERIFY(db->openAndMigrate());
    masterRepository = std::make_unique<MasterDataRepository>(db->database());
    master = std::make_unique<MasterDataService>(*masterRepository);
    documents = std::make_unique<DocumentRepository>(db->database());
    inventory = std::make_unique<InventoryRepository>(db->database());
    inbound = std::make_unique<InboundService>(db->database(), *masterRepository,
                                               *documents, *inventory);
    logistics = std::make_unique<LogisticsService>(db->database(), *masterRepository,
                                                    *documents, *inventory);
    queries = std::make_unique<InventoryQueryService>(db->database());

    const auto category = master->createCategory(QStringLiteral("设备"));
    QVERIFY(category.ok());
    const auto type = master->createProductType(category.value().id,
                                                 QStringLiteral("保险箱"), {});
    QVERIFY(type.ok());
    productTypeId = type.value().id;
    const auto model = master->createProductModel(productTypeId, QStringLiteral("F25"), {});
    QVERIFY(model.ok());
    modelId = model.value().id;
    companyId = addLocation(QStringLiteral("公司仓"), LocationType::Company, true);
    factoryId = addLocation(QStringLiteral("工厂A"), LocationType::Factory, true);
    externalId = addLocation(QStringLiteral("客户现场"), LocationType::Customer, false);
    workflow = std::make_unique<BatchWorkflowService>(*master, *documents, *inventory,
                                                       *queries);
}

EntityId BatchWorkflowServiceTest::addLocation(const QString &name, LocationType type,
                                               bool countsAsInventory)
{
    const auto result = master->createLocation(name, type, countsAsInventory);
    return result.ok() ? result.value().id : 0;
}

EntityId BatchWorkflowServiceTest::createConfirmedBatch(const QString &batchNo, qint64 quantity)
{
    const auto created = inbound->createDraft({batchNo, productTypeId, quantity, companyId,
                                                QDateTime::currentDateTime(), {}});
    if (!created.ok() || !inbound->confirm(created.value()).ok())
        return 0;
    return created.value();
}

Result<EntityId> BatchWorkflowServiceTest::createLogistics(
    const QString &number, DocumentType type, EntityId source, EntityId destination,
    EntityId batch, qint64 quantity, EntityId model)
{
    const auto created = logistics->createDraft({number, type, source, destination,
        QDateTime::currentDateTime(), {}, {{batch, quantity, model}}});
    if (!created.ok())
        return created;
    const auto confirmed = logistics->confirm(created.value());
    if (!confirmed.ok())
        return Result<EntityId>::failure(confirmed.error());
    return created;
}

bool BatchWorkflowServiceTest::containsAction(const QVector<WorkflowAction> &actions,
                                               WorkflowAction action) const
{
    return std::find(actions.cbegin(), actions.cend(), action) != actions.cend();
}

void BatchWorkflowServiceTest::draftInboundIsInProgressAndBlocksOutbound()
{
    const auto created = inbound->createDraft({QStringLiteral("B-DRAFT"), productTypeId, 10,
        companyId, QDateTime::currentDateTime(), {}});
    QVERIFY(created.ok());

    const auto state = workflow->state(created.value());
    QVERIFY(state.ok());
    QCOMPARE(state.value().steps.at(1).status, WorkflowStepStatus::InProgress);
    QVERIFY(!containsAction(state.value().availableActions, WorkflowAction::CreateOutbound));
    QCOMPARE(state.value().nextPage, 1);
}

void BatchWorkflowServiceTest::confirmedInboundOffersDirectOutboundOrTransfer()
{
    const EntityId batch = createConfirmedBatch(QStringLiteral("B-CONFIRMED"), 10);
    QVERIFY(batch > 0);

    const auto state = workflow->state(batch);
    QVERIFY(state.ok());
    QCOMPARE(state.value().steps.at(1).status, WorkflowStepStatus::Completed);
    QVERIFY(containsAction(state.value().availableActions, WorkflowAction::CreateOutbound));
    QVERIFY(containsAction(state.value().availableActions, WorkflowAction::CreateTransfer));
    QCOMPARE(state.value().nextPage, 3);
}

void BatchWorkflowServiceTest::confirmedTransferShowsDistributedStockAndOutboundNext()
{
    const EntityId batch = createConfirmedBatch(QStringLiteral("B-TRANSFER"), 10);
    QVERIFY(batch > 0);
    const auto transfer = createLogistics(QStringLiteral("T-1"), DocumentType::Transfer,
                                          companyId, factoryId, batch, 5);
    QVERIFY(transfer.ok());

    const auto state = workflow->state(batch);
    QVERIFY(state.ok());
    QCOMPARE(state.value().currentLocations.size(), 2);
    QCOMPARE(state.value().steps.at(2).status, WorkflowStepStatus::Completed);
    QVERIFY(containsAction(state.value().availableActions, WorkflowAction::CreateOutbound));
    QCOMPARE(state.value().nextPage, 3);
}

void BatchWorkflowServiceTest::outboundThenReturnOffersReprocess()
{
    const EntityId batch = createConfirmedBatch(QStringLiteral("B-RETURN"), 10);
    QVERIFY(batch > 0);
    const auto outbound = createLogistics(QStringLiteral("O-1"), DocumentType::Outbound,
                                          companyId, externalId, batch, 10, modelId);
    QVERIFY(outbound.ok());
    const auto returned = createLogistics(QStringLiteral("R-1"), DocumentType::Return,
                                          externalId, companyId, batch, 3, modelId);
    QVERIFY(returned.ok());

    const auto state = workflow->state(batch);
    QVERIFY(state.ok());
    QCOMPARE(state.value().currentQuantity, qint64(3));
    QVERIFY(containsAction(state.value().availableActions, WorkflowAction::CreateOutbound));
    QCOMPARE(state.value().nextPage, 3);
}

void BatchWorkflowServiceTest::zeroBalanceHistoryRemainsTrackableAndCompleted()
{
    const EntityId batch = createConfirmedBatch(QStringLiteral("B-ZERO"), 10);
    QVERIFY(batch > 0);
    const auto outbound = createLogistics(QStringLiteral("O-ZERO"), DocumentType::Outbound,
                                          companyId, externalId, batch, 10, modelId);
    QVERIFY(outbound.ok());

    const auto state = workflow->state(batch);
    QVERIFY(state.ok());
    QCOMPARE(state.value().currentQuantity, qint64(0));
    QCOMPARE(state.value().steps.at(5).status, WorkflowStepStatus::Completed);
    QVERIFY(containsAction(state.value().availableActions, WorkflowAction::ViewTracking));
}

void BatchWorkflowServiceTest::directOutboundCompletesWorkflow()
{
    const EntityId batch = createConfirmedBatch(QStringLiteral("B-DIRECT"), 10);
    QVERIFY(batch > 0);
    const auto outbound = createLogistics(QStringLiteral("O-DIRECT"), DocumentType::Outbound,
                                          companyId, externalId, batch, 10, modelId);
    QVERIFY(outbound.ok());
    const auto state = workflow->state(batch);
    QVERIFY(state.ok());
    QCOMPARE(state.value().currentQuantity, qint64(0));
    QCOMPARE(state.value().steps.at(3).status, WorkflowStepStatus::Completed);
    QCOMPARE(state.value().steps.at(5).status, WorkflowStepStatus::Completed);
    QCOMPARE(state.value().nextPage, 5);
    QVERIFY(containsAction(state.value().availableActions, WorkflowAction::ViewTracking));
}

void BatchWorkflowServiceTest::cancelledOutboundReopensAvailableStock()
{
    const EntityId batch = createConfirmedBatch(QStringLiteral("B-CANCEL-OUT"), 10);
    QVERIFY(batch > 0);
    const auto outbound = logistics->createDraft({QStringLiteral("O-CANCEL-OUT"), DocumentType::Outbound,
        companyId, externalId, QDateTime::currentDateTime(), {}, {{batch, 10, modelId}}});
    QVERIFY(outbound.ok());
    QVERIFY(logistics->confirm(outbound.value()).ok());
    QVERIFY(logistics->cancel(outbound.value()).ok());
    const auto state = workflow->state(batch);
    QVERIFY(state.ok());
    QCOMPARE(state.value().currentQuantity, qint64(10));
    QVERIFY(containsAction(state.value().availableActions, WorkflowAction::CreateOutbound));
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
    const auto transfer = createLogistics(QStringLiteral("T-FIRST"), DocumentType::Transfer,
                                          companyId, factoryId, first, 4);
    QVERIFY(transfer.ok());

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
    QVERIFY(createLogistics(QStringLiteral("O-PARTIAL"), DocumentType::Outbound,
                             companyId, externalId, batch, 6, modelId).ok());
    const auto state = workflow->state(batch);
    QVERIFY(state.ok());
    QCOMPARE(state.value().currentQuantity, qint64(4));
    QVERIFY(state.value().availableActions.contains(WorkflowAction::CreateReturn));
    QVERIFY(workflow->canStartAction(batch, WorkflowAction::CreateReturn).ok());
    QVERIFY(workflow->canStartAction(batch, WorkflowAction::CreateOutbound).ok());
}

void BatchWorkflowServiceTest::pendingDraftBlocksNewActions_data()
{
    QTest::addColumn<DocumentType>("type");
    QTest::newRow("transfer") << DocumentType::Transfer;
    QTest::newRow("outbound") << DocumentType::Outbound;
    QTest::newRow("return") << DocumentType::Return;
}

void BatchWorkflowServiceTest::pendingDraftBlocksNewActions()
{
    QFETCH(DocumentType, type);
    const EntityId batch = createConfirmedBatch(QStringLiteral("B-PENDING"), 10);
    QVERIFY(batch > 0);
    if (type == DocumentType::Return)
        QVERIFY(createLogistics(QStringLiteral("O-PENDING"), DocumentType::Outbound,
                                 companyId, externalId, batch, 10).ok());
    const auto draft = logistics->createDraft({QStringLiteral("L-PENDING"), type,
        type == DocumentType::Return ? externalId : companyId,
        type == DocumentType::Transfer ? factoryId : type == DocumentType::Return ? companyId : externalId,
        QDateTime::currentDateTime(), {}, {{batch, 1}}});
    QVERIFY(draft.ok());
    const auto state = workflow->state(batch);
    QVERIFY(state.ok());
    QCOMPARE(state.value().nextPage, type == DocumentType::Transfer ? 2 : type == DocumentType::Return ? 8 : 3);
    for (const auto action : {WorkflowAction::CreateTransfer, WorkflowAction::CreateOutbound,
                              WorkflowAction::CreateReturn}) {
        const auto allowed = workflow->canStartAction(batch, action);
        QVERIFY(!allowed.ok());
        QVERIFY(allowed.error().message.contains(QStringLiteral("草稿")));
    }
}

QTEST_MAIN(BatchWorkflowServiceTest)
#include "tst_batch_workflow_service.moc"
