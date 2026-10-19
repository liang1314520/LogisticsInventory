#include "repositories/document_repository.h"
#include "repositories/inventory_repository.h"
#include "repositories/master_data_repository.h"
#include "services/batch_workflow_service.h"
#include "services/inbound_service.h"
#include "services/inventory_query_service.h"
#include "services/logistics_service.h"
#include "services/master_data_service.h"
#include "test_support.h"
#include "ui/widgets/workflow_bar.h"

#include <memory>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QtTest>

class WorkflowBarTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void rendersSixNumberedSteps();
    void listsBatchesAndEmitsBatchChanged();
    void nextButtonUsesWorkflowTarget();
    void trackingButtonEmitsCurrentBatch();
    void clearsStateWhenNoBatchExists();

private:
    EntityId createConfirmedBatch(const QString &batchNo, qint64 quantity);

    std::unique_ptr<TestDatabase> db;
    std::unique_ptr<MasterDataRepository> masterRepository;
    std::unique_ptr<MasterDataService> master;
    std::unique_ptr<DocumentRepository> documents;
    std::unique_ptr<InventoryRepository> inventory;
    std::unique_ptr<InboundService> inbound;
    std::unique_ptr<InventoryQueryService> queries;
    std::unique_ptr<BatchWorkflowService> workflow;
    EntityId productTypeId = 0;
    EntityId locationId = 0;
};

void WorkflowBarTest::init()
{
    db = std::make_unique<TestDatabase>();
    QVERIFY(db->openAndMigrate());
    masterRepository = std::make_unique<MasterDataRepository>(db->database());
    master = std::make_unique<MasterDataService>(*masterRepository);
    documents = std::make_unique<DocumentRepository>(db->database());
    inventory = std::make_unique<InventoryRepository>(db->database());
    inbound = std::make_unique<InboundService>(db->database(), *masterRepository,
                                                *documents, *inventory);
    queries = std::make_unique<InventoryQueryService>(db->database());
    const auto category = master->createCategory(QStringLiteral("设备"));
    QVERIFY(category.ok());
    const auto type = master->createProductType(category.value().id,
                                                 QStringLiteral("保险箱"), {});
    QVERIFY(type.ok());
    productTypeId = type.value().id;
    const auto location = master->createLocation(QStringLiteral("公司仓"),
                                                 LocationType::Company, true);
    QVERIFY(location.ok());
    locationId = location.value().id;
    workflow = std::make_unique<BatchWorkflowService>(*master, *documents, *inventory,
                                                       *queries);
}

EntityId WorkflowBarTest::createConfirmedBatch(const QString &batchNo, qint64 quantity)
{
    const auto created = inbound->createDraft({batchNo, productTypeId, quantity, locationId,
                                                QDateTime::currentDateTime(), {}});
    if (!created.ok() || !inbound->confirm(created.value()).ok())
        return 0;
    return created.value();
}

void WorkflowBarTest::rendersSixNumberedSteps()
{
    WorkflowBar bar(*workflow);
    QCOMPARE(bar.objectName(), QStringLiteral("workflowBar"));
    auto *steps = bar.findChild<QTableWidget *>(QStringLiteral("workflowSteps"));
    QVERIFY(steps);
    QCOMPARE(steps->columnCount(), 3);
}

void WorkflowBarTest::listsBatchesAndEmitsBatchChanged()
{
    const EntityId first = createConfirmedBatch(QStringLiteral("B-1"), 10);
    const EntityId second = createConfirmedBatch(QStringLiteral("B-2"), 20);
    QVERIFY(first > 0 && second > 0);
    WorkflowBar bar(*workflow);
    QVERIFY(bar.refresh().ok());
    auto *selector = bar.findChild<QComboBox *>(QStringLiteral("workflowBatchSelector"));
    QVERIFY(selector);
    QVERIFY(selector->findData(first) >= 0);
    QVERIFY(selector->findData(second) >= 0);
    QSignalSpy changed(&bar, &WorkflowBar::batchChanged);
    const EntityId target = selector->currentData().toLongLong() == first ? second : first;
    selector->setCurrentIndex(selector->findData(target));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(bar.currentBatchId(), target);
}

void WorkflowBarTest::nextButtonUsesWorkflowTarget()
{
    const EntityId batch = createConfirmedBatch(QStringLiteral("B-2"), 10);
    QVERIFY(batch > 0);
    WorkflowBar bar(*workflow);
    QVERIFY(bar.setBatch(batch).ok());
    QSignalSpy next(&bar, &WorkflowBar::nextRequested);
    QVERIFY(QMetaObject::invokeMethod(&bar, "requestNext"));
    QCOMPARE(next.count(), 1);
    QCOMPARE(next.at(0).at(0).toInt(), 3);
    QCOMPARE(next.at(0).at(1).toLongLong(), batch);
}

void WorkflowBarTest::trackingButtonEmitsCurrentBatch()
{
    const EntityId batch = createConfirmedBatch(QStringLiteral("B-3"), 10);
    QVERIFY(batch > 0);
    WorkflowBar bar(*workflow);
    QVERIFY(bar.setBatch(batch).ok());
    QSignalSpy tracking(&bar, &WorkflowBar::trackingRequested);
    QVERIFY(QMetaObject::invokeMethod(&bar, "requestTracking"));
    QCOMPARE(tracking.count(), 1);
    QCOMPARE(tracking.at(0).at(0).toLongLong(), batch);
}

void WorkflowBarTest::clearsStateWhenNoBatchExists()
{
    WorkflowBar bar(*workflow);
    QVERIFY(bar.refresh().ok());
    auto *selector = bar.findChild<QComboBox *>(QStringLiteral("workflowBatchSelector"));
    auto *status = bar.findChild<QLabel *>(QStringLiteral("workflowStatusLabel"));
    auto *next = bar.findChild<QPushButton *>(QStringLiteral("workflowNextButton"));
    QVERIFY(selector && status && next);
    QCOMPARE(selector->count(), 0);
    QVERIFY(status->text().isEmpty());
    QVERIFY(!next->isEnabled());
    QCOMPARE(bar.currentBatchId(), EntityId(0));
}

QTEST_MAIN(WorkflowBarTest)
#include "tst_workflow_bar.moc"
