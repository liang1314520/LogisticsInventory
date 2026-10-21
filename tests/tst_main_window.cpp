#include "repositories/document_repository.h"
#include "repositories/inventory_repository.h"
#include "repositories/master_data_repository.h"
#include "services/inbound_service.h"
#include "services/inventory_query_service.h"
#include "services/logistics_service.h"
#include "services/master_data_service.h"
#include "services/batch_workflow_service.h"
#include "test_support.h"
#include "ui/main_window.h"
#include "ui/pages/dashboard_page.h"
#include "ui/pages/batch_tracking_page.h"
#include "ui/pages/inbound_page.h"
#include "ui/pages/master_data_page.h"
#include "ui/dialogs/inbound_dialog.h"
#include "ui/dialogs/logistics_dialog.h"
#include "ui/pages/logistics_page.h"
#include "ui/widgets/workflow_bar.h"

#include <memory>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QtTest>

class MainWindowTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void navigationHasNineEntriesAndSwitchesPages();
    void setPageOnlyReplacesPlaceholdersAndKeepsOwnership();
    void dashboardAndInboundPageReflectDocumentsAndActions();
    void transferAndOutboundPagesFilterSortAndMutate();
    void successfulMutationRefreshesDashboardAndCurrentPage();
    void workflowBarIsAlwaysVisible();
    void selectingBatchUpdatesWorkflowBar();
    void mutationRefreshesWorkflowState();
    void nextActionNavigatesToExpectedPage();
    void inboundPageSelectsWorkflowBatch();
    void logisticsPageSelectsWorkflowBatch();
    void outboundCreateShowsCancelledInboundPrerequisite();
    void returnPageAllowsZeroFactoryBalance();
    void historyViewRemainsAvailableWhenWorkflowBlocksNewAction();
    void workflowContextAppearsOnTrackingAndMasterDataPages();
    void trackingPageKeepsSelectedWorkflowBatch();
    void logisticsSelectsBatchFromAnyDocumentLine();
    void cancellingCurrentRecordKeepsWorkflowContext();
private:
    Result<EntityId> createBatch(const QString &number, qint64 quantity,
                                const QDateTime &at = QDateTime::currentDateTime());
    std::unique_ptr<TestDatabase> db;
    std::unique_ptr<MasterDataRepository> masterRepository;
    std::unique_ptr<MasterDataService> masterService;
    std::unique_ptr<DocumentRepository> documents;
    std::unique_ptr<InventoryRepository> inventory;
    std::unique_ptr<InboundService> inbound;
    std::unique_ptr<LogisticsService> logistics;
    std::unique_ptr<InventoryQueryService> queries;
    std::unique_ptr<BatchWorkflowService> workflow;
    std::unique_ptr<MainWindow> window;
    EntityId productTypeId = 0, locationId = 0, factoryId = 0, externalId = 0, personId = 0;
};

void MainWindowTest::init()
{
    window.reset();
    db = std::make_unique<TestDatabase>();
    QVERIFY(db->openAndMigrate());
    masterRepository = std::make_unique<MasterDataRepository>(db->database());
    masterService = std::make_unique<MasterDataService>(*masterRepository);
    documents = std::make_unique<DocumentRepository>(db->database());
    inventory = std::make_unique<InventoryRepository>(db->database());
    inbound = std::make_unique<InboundService>(db->database(), *masterRepository, *documents, *inventory);
    logistics = std::make_unique<LogisticsService>(db->database(), *masterRepository, *documents, *inventory);
    queries = std::make_unique<InventoryQueryService>(db->database());
    workflow = std::make_unique<BatchWorkflowService>(*masterService, *documents, *inventory, *queries);
    const auto category = masterService->createCategory(QStringLiteral("设备"));
    QVERIFY(category.ok());
    const auto product = masterService->createProductType(category.value().id, QStringLiteral("GW-1"), {});
    const auto company = masterService->createLocation(QStringLiteral("公司仓"), LocationType::Company, true);
    const auto factory = masterService->createLocation(QStringLiteral("工厂"), LocationType::Factory, true);
    const auto external = masterService->createLocation(QStringLiteral("Amazon"), LocationType::Other, false);
    const auto person = masterService->createPersonnel(QStringLiteral("经办人"));
    QVERIFY(product.ok() && company.ok() && factory.ok() && external.ok() && person.ok());
    productTypeId = product.value().id;
    locationId = company.value().id;
    factoryId = factory.value().id;
    externalId = external.value().id;
    personId = person.value().id;
    window = std::make_unique<MainWindow>(*masterService, *queries, *inbound, *logistics, *workflow);
    window->findChild<InboundPage *>(QStringLiteral("inboundPage"))->setConfirmationCallback([](const QString &) { return true; });
    window->findChild<LogisticsPage *>(QStringLiteral("transferPage"))->setConfirmationCallback([](const QString &) { return true; });
    window->findChild<LogisticsPage *>(QStringLiteral("outboundPage"))->setConfirmationCallback([](const QString &) { return true; });
}

Result<EntityId> MainWindowTest::createBatch(const QString &number, qint64 quantity, const QDateTime &at)
{
    return inbound->create({number, productTypeId, quantity, locationId, at, {}, QStringLiteral("V40"), personId});
}

void MainWindowTest::navigationHasNineEntriesAndSwitchesPages()
{
    auto *navigation = window->findChild<QListWidget *>(QStringLiteral("navigation"));
    auto *pages = window->findChild<QStackedWidget *>(QStringLiteral("pages"));
    QVERIFY(navigation && pages);
    QCOMPARE(navigation->count(), 9);
    QCOMPARE(pages->count(), 9);
    QCOMPARE(navigation->item(0)->text(), QStringLiteral("工作台"));
    QCOMPARE(navigation->item(1)->text(), QStringLiteral("01 基础资料"));
    QCOMPARE(navigation->item(5)->text(), QStringLiteral("05 退回管理"));
    navigation->setCurrentRow(5);
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("returnPage"));
    navigation->setCurrentRow(4);
    QCOMPARE(pages->currentIndex(), 3);
}

void MainWindowTest::setPageOnlyReplacesPlaceholdersAndKeepsOwnership()
{
    auto *pages = window->findChild<QStackedWidget *>(QStringLiteral("pages"));
    auto *dashboard = window->findChild<DashboardPage *>(QStringLiteral("dashboardPage"));
    QVERIFY(pages && dashboard);
    for (int index = 0; index < 4; ++index) {
        QWidget *corePage = pages->widget(index);
        auto *rejected = new QWidget(window.get());
        window->setPage(index, rejected);
        QCOMPARE(pages->widget(index), corePage);
        QVERIFY(pages->indexOf(rejected) < 0);
    }
    QVERIFY(dashboard->refresh().ok());
    for (int index = 4; index < 8; ++index) {
        auto *replacement = new QWidget;
        replacement->setObjectName(QStringLiteral("replacement%1").arg(index));
        window->setPage(index, replacement);
        QCOMPARE(pages->widget(index), replacement);
        QCOMPARE(replacement->parentWidget(), pages);
        QSignalSpy destroyed(replacement, &QObject::destroyed);
        window->setPage(index, replacement);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(destroyed.count(), 0);
        QCOMPARE(pages->widget(index), replacement);
    }
}

void MainWindowTest::dashboardAndInboundPageReflectDocumentsAndActions()
{
    const auto oldId = createBatch(QStringLiteral("B-OLD"), 10,
        QDateTime::fromString(QStringLiteral("2026-08-01T08:00:00"), Qt::ISODate));
    const auto recentId = createBatch(QStringLiteral("B-NEW"), 25);
    QVERIFY(oldId.ok() && recentId.ok());
    auto *dashboard = window->findChild<DashboardPage *>(QStringLiteral("dashboardPage"));
    auto *page = window->findChild<InboundPage *>(QStringLiteral("inboundPage"));
    QVERIFY(dashboard->refresh().ok());
    QVERIFY(page->refresh().ok());
    QCOMPARE(dashboard->findChild<QLabel *>(QStringLiteral("totalInventory"))->text(), QStringLiteral("35"));
    auto *table = page->findChild<QTableWidget *>(QStringLiteral("documents"));
    QCOMPARE(table->rowCount(), 2);
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("B-NEW"));
    for (int row : {0, 1}) {
        table->selectRow(row);
        QVERIFY(page->findChild<QPushButton *>(QStringLiteral("viewButton"))->isEnabled());
        QVERIFY(page->findChild<QPushButton *>(QStringLiteral("cancelButton"))->isEnabled());
        QVERIFY(page->findChild<QPushButton *>(QStringLiteral("editButton"))->isEnabled());
    }
}

void MainWindowTest::transferAndOutboundPagesFilterSortAndMutate()
{
    const auto batch = createBatch(QStringLiteral("B-STOCK"), 500,
        QDateTime::fromString(QStringLiteral("2026-07-01T08:00:00"), Qt::ISODate));
    QVERIFY(batch.ok());
    for (int index = 0; index < 3; ++index) {
        const auto date = QDateTime::fromString(QStringLiteral("2026-0%1-01T10:00:00").arg(index + 7), Qt::ISODate);
        const auto transfer = logistics->create({QStringLiteral("T-%1").arg(index), DocumentType::Transfer,
            locationId, factoryId, date, {}, {{batch.value(), 20}}});
        const auto outbound = logistics->create({QStringLiteral("O-%1").arg(index), DocumentType::Outbound,
            factoryId, externalId, date, {}, {{batch.value(), 20}}, personId});
        QVERIFY(transfer.ok() && outbound.ok());
        if (index == 2) {
            QVERIFY(logistics->cancel(transfer.value()).ok());
            QVERIFY(logistics->cancel(outbound.value()).ok());
        }
    }
    auto *navigation = window->findChild<QListWidget *>(QStringLiteral("navigation"));
    for (int navRow : {3, 4}) {
        navigation->setCurrentRow(navRow);
        auto *page = qobject_cast<LogisticsPage *>(window->findChild<QStackedWidget *>(QStringLiteral("pages"))->currentWidget());
        QVERIFY(page);
        const QString prefix = navRow == 3 ? QStringLiteral("T") : QStringLiteral("O");
        auto *table = page->findChild<QTableWidget *>(QStringLiteral("documents"));
        QCOMPARE(table->rowCount(), 3);
        QCOMPARE(table->item(0, 1)->text(), prefix + QStringLiteral("-2"));
        QCOMPARE(table->item(1, 1)->text(), prefix + QStringLiteral("-1"));
        QCOMPARE(table->item(2, 1)->text(), prefix + QStringLiteral("-0"));
        table->selectRow(0);
        QVERIFY(page->findChild<QPushButton *>(QStringLiteral("editButton"))->isEnabled());
        QVERIFY(!page->findChild<QPushButton *>(QStringLiteral("cancelButton"))->isEnabled());
        table->selectRow(1);
        const EntityId id = table->item(1, 0)->data(Qt::UserRole + 1).toLongLong();
        QSignalSpy changed(page, &LogisticsPage::dataChanged);
        QVERIFY(QMetaObject::invokeMethod(page, "cancelSelected"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(table->item(1, 3)->text(), QStringLiteral("已作废"));
        bool restored = false;
        QTimer::singleShot(0, page, [page, &restored] {
            if (auto *dialog = page->findChild<LogisticsDialog *>()) {
                restored = dialog->save();
                restored ? dialog->accept() : dialog->reject();
            }
        });
        QVERIFY(page->openEditDialog(id).ok());
        QVERIFY(restored);
        QCOMPARE(changed.count(), 2);
        QCOMPARE(table->rowCount(), 3);
        QCOMPARE(table->item(1, 3)->text(), QStringLiteral("已确认"));
    }
    auto *dashboard = window->findChild<DashboardPage *>(QStringLiteral("dashboardPage"));
    QCOMPARE(dashboard->findChild<QLabel *>(QStringLiteral("totalInventory"))->text(), QStringLiteral("460"));
}

void MainWindowTest::successfulMutationRefreshesDashboardAndCurrentPage()
{
    const auto id = createBatch(QStringLiteral("B-CANCEL"), 30);
    QVERIFY(id.ok());
    window->findChild<QListWidget *>(QStringLiteral("navigation"))->setCurrentRow(2);
    auto *page = window->findChild<InboundPage *>(QStringLiteral("inboundPage"));
    auto *table = page->findChild<QTableWidget *>(QStringLiteral("documents"));
    QVERIFY(page->selectBatch(id.value()).ok());
    QSignalSpy changed(page, &InboundPage::dataChanged);
    QVERIFY(QMetaObject::invokeMethod(page, "cancelSelected"));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(table->item(0, 4)->text(), QStringLiteral("已作废"));
    auto *dashboard = window->findChild<DashboardPage *>(QStringLiteral("dashboardPage"));
    QCOMPARE(dashboard->findChild<QLabel *>(QStringLiteral("totalInventory"))->text(), QStringLiteral("0"));
    bool restored = false;
    QTimer::singleShot(0, page, [page, &restored] {
        if (auto *dialog = page->findChild<InboundDialog *>()) {
            restored = dialog->save();
            restored ? dialog->accept() : dialog->reject();
        }
    });
    QVERIFY(page->openEditDialog(id.value()).ok());
    QVERIFY(restored);
    QCOMPARE(changed.count(), 2);
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(dashboard->findChild<QLabel *>(QStringLiteral("totalInventory"))->text(), QStringLiteral("30"));
}

void MainWindowTest::workflowBarIsAlwaysVisible()
{
    auto *bar = window->findChild<WorkflowBar *>(QStringLiteral("workflowBar"));
    QVERIFY(bar);
    QVERIFY(bar->findChild<QComboBox *>(QStringLiteral("workflowBatchSelector")));
    QVERIFY(bar->findChild<QTableWidget *>(QStringLiteral("workflowSteps")));
}

void MainWindowTest::selectingBatchUpdatesWorkflowBar()
{
    const auto created = createBatch(QStringLiteral("B-WORKFLOW"), 12);
    QVERIFY(created.ok());
    auto *bar = window->findChild<WorkflowBar *>(QStringLiteral("workflowBar"));
    QVERIFY(bar->refresh().ok());
    auto *selector = bar->findChild<QComboBox *>(QStringLiteral("workflowBatchSelector"));
    selector->setCurrentIndex(selector->findData(created.value()));
    QCoreApplication::processEvents();
    QCOMPARE(window->currentBatchId(), created.value());
    QCOMPARE(bar->currentBatchId(), created.value());
}

void MainWindowTest::mutationRefreshesWorkflowState()
{
    const auto created = createBatch(QStringLiteral("B-MUTATION"), 12);
    QVERIFY(created.ok());
    window->setCurrentBatch(created.value());
    auto *bar = window->findChild<WorkflowBar *>(QStringLiteral("workflowBar"));
    QVERIFY(bar->refresh().ok());
    InboundInput changed = inbound->byId(created.value()).value();
    changed.quantity = 20;
    QVERIFY(inbound->update(created.value(), changed).ok());
    QVERIFY(QMetaObject::invokeMethod(window.get(), "refreshAfterMutation"));
    QVERIFY(bar->findChild<QLabel *>(QStringLiteral("workflowStatusLabel"))->text().contains(QStringLiteral("进行中")));
    QVERIFY(bar->findChild<QLabel *>(QStringLiteral("workflowMessageLabel"))->text().contains(QStringLiteral("调拨")));
    QVERIFY(bar->findChild<QComboBox *>(QStringLiteral("workflowBatchSelector"))->currentText().contains(QStringLiteral("20")));
}

void MainWindowTest::nextActionNavigatesToExpectedPage()
{
    const auto created = createBatch(QStringLiteral("B-NEXT"), 12);
    QVERIFY(created.ok());
    window->setCurrentBatch(created.value());
    auto *bar = window->findChild<WorkflowBar *>(QStringLiteral("workflowBar"));
    QVERIFY(bar->refresh().ok());
    auto *next = bar->findChild<QPushButton *>(QStringLiteral("workflowNextButton"));
    QVERIFY(next->isEnabled());
    next->click();
    QCOMPARE(window->findChild<QStackedWidget *>(QStringLiteral("pages"))->currentIndex(), 2);
}

void MainWindowTest::inboundPageSelectsWorkflowBatch()
{
    const auto first = createBatch(QStringLiteral("B-SELECT"), 12);
    const auto second = createBatch(QStringLiteral("B-OTHER"), 9);
    QVERIFY(first.ok() && second.ok());
    window->setCurrentBatch(first.value());
    window->findChild<QListWidget *>(QStringLiteral("navigation"))->setCurrentRow(2);
    auto *page = window->findChild<InboundPage *>(QStringLiteral("inboundPage"));
    auto *table = page->findChild<QTableWidget *>(QStringLiteral("documents"));
    QVERIFY(table->currentRow() >= 0);
    QCOMPARE(table->item(table->currentRow(), 0)->data(Qt::UserRole + 1).toLongLong(), first.value());
    QVERIFY(page->findChild<QLabel *>(QStringLiteral("workflowContext"))->text().contains(QStringLiteral("步骤 02 / 06")));
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 0)->data(Qt::UserRole + 1).toLongLong() == second.value())
            table->selectRow(row);
    }
    QCOMPARE(window->currentBatchId(), second.value());
}

void MainWindowTest::logisticsPageSelectsWorkflowBatch()
{
    const auto batch = createBatch(QStringLiteral("B-LOGISTICS"), 12);
    QVERIFY(batch.ok());
    const auto earlier = logistics->create({QStringLiteral("T-SELECT"), DocumentType::Transfer,
        locationId, factoryId, QDateTime::currentDateTime().addSecs(-60), {}, {{batch.value(), 2}}});
    const auto latest = logistics->create({QStringLiteral("T-HISTORY"), DocumentType::Transfer,
        locationId, factoryId, QDateTime::currentDateTime(), {}, {{batch.value(), 1}}});
    QVERIFY(earlier.ok() && latest.ok());
    window->setCurrentBatch(batch.value());
    window->findChild<QListWidget *>(QStringLiteral("navigation"))->setCurrentRow(3);
    auto *page = window->findChild<LogisticsPage *>(QStringLiteral("transferPage"));
    auto *table = page->findChild<QTableWidget *>(QStringLiteral("documents"));
    QVERIFY(table->currentRow() >= 0);
    QCOMPARE(table->item(table->currentRow(), 0)->data(Qt::UserRole + 1).toLongLong(), latest.value());
    QVERIFY(page->findChild<QPushButton *>(QStringLiteral("editButton"))->isEnabled());
}

void MainWindowTest::outboundCreateShowsCancelledInboundPrerequisite()
{
    const auto batch = createBatch(QStringLiteral("B-BLOCKED"), 12);
    QVERIFY(batch.ok());
    QVERIFY(inbound->cancel(batch.value()).ok());
    window->setCurrentBatch(batch.value());
    window->findChild<QListWidget *>(QStringLiteral("navigation"))->setCurrentRow(4);
    auto *page = window->findChild<LogisticsPage *>(QStringLiteral("outboundPage"));
    QTimer::singleShot(0, page, [page] {
        if (auto *dialog = page->findChild<LogisticsDialog *>()) dialog->reject();
    });
    const auto result = page->openCreateDialog();
    QVERIFY(!result.ok());
    QVERIFY(result.error().message.contains(QStringLiteral("已作废")));
    QCOMPARE(page->findChild<QLabel *>(QStringLiteral("errorLabel"))->text(), result.error().message);
    QVERIFY(logistics->list(DocumentType::Outbound).value().isEmpty());
}

void MainWindowTest::returnPageAllowsZeroFactoryBalance()
{
    const auto batch = createBatch(QStringLiteral("B-NO-RETURN"), 12);
    QVERIFY(batch.ok());
    window->setCurrentBatch(batch.value());
    window->findChild<QListWidget *>(QStringLiteral("navigation"))->setCurrentRow(5);
    auto *page = window->findChild<LogisticsPage *>(QStringLiteral("returnPage"));
    bool opened = false;
    QTimer::singleShot(0, page, [page, &opened] {
        if (auto *dialog = page->findChild<LogisticsDialog *>()) {
            opened = true;
            dialog->reject();
        }
    });
    QVERIFY(page->openCreateDialog().ok());
    QVERIFY(opened);
}

void MainWindowTest::historyViewRemainsAvailableWhenWorkflowBlocksNewAction()
{
    const auto batch = createBatch(QStringLiteral("B-HISTORY"), 12);
    QVERIFY(batch.ok());
    const auto outbound = logistics->create({QStringLiteral("O-HISTORY"), DocumentType::Outbound,
        factoryId, externalId, QDateTime::currentDateTime(), {}, {{batch.value(), 12}}, personId});
    QVERIFY(outbound.ok());
    QVERIFY(masterService->setProductTypeEnabled(productTypeId, false).ok());
    window->setCurrentBatch(batch.value());
    window->findChild<QListWidget *>(QStringLiteral("navigation"))->setCurrentRow(4);
    auto *page = window->findChild<LogisticsPage *>(QStringLiteral("outboundPage"));
    QTimer::singleShot(0, page, [page] {
        if (auto *dialog = page->findChild<LogisticsDialog *>()) dialog->reject();
    });
    QVERIFY(!page->openCreateDialog().ok());
    QVERIFY(page->findChild<QPushButton *>(QStringLiteral("viewButton"))->isEnabled());
    QCoreApplication::processEvents();
    bool viewed = false;
    QTimer::singleShot(0, page, [page, &viewed] {
        if (auto *dialog = page->findChild<LogisticsDialog *>()) {
            viewed = dialog->isReadOnly();
            dialog->reject();
        }
    });
    QVERIFY(page->openViewDialog(outbound.value()).ok());
    QVERIFY(viewed);
    window->findChild<WorkflowBar *>(QStringLiteral("workflowBar"))
        ->findChild<QPushButton *>(QStringLiteral("workflowTrackingButton"))->click();
    auto *tracking = window->findChild<BatchTrackingPage *>(QStringLiteral("batchTrackingPage"));
    QCOMPARE(window->findChild<QStackedWidget *>(QStringLiteral("pages"))->currentIndex(), 5);
    QCOMPARE(tracking->findChild<QComboBox *>(QStringLiteral("batchSelector"))->currentData().toLongLong(), batch.value());
    QCOMPARE(tracking->findChild<QTableWidget *>(QStringLiteral("timeline"))->rowCount(), 2);
}

void MainWindowTest::workflowContextAppearsOnTrackingAndMasterDataPages()
{
    const auto batch = createBatch(QStringLiteral("B-CONTEXT"), 12);
    QVERIFY(batch.ok());
    window->setCurrentBatch(batch.value());
    auto *navigation = window->findChild<QListWidget *>(QStringLiteral("navigation"));
    navigation->setCurrentRow(6);
    auto *tracking = window->findChild<BatchTrackingPage *>(QStringLiteral("batchTrackingPage"));
    QVERIFY(tracking->findChild<QLabel *>(QStringLiteral("workflowContext"))->text().contains(QStringLiteral("步骤 06 / 06")));
    navigation->setCurrentRow(1);
    auto *masterPage = window->findChild<MasterDataPage *>(QStringLiteral("masterDataPage"));
    QVERIFY(masterPage->findChild<QLabel *>(QStringLiteral("workflowContext"))->text().contains(QStringLiteral("步骤 01 / 06")));
}

void MainWindowTest::trackingPageKeepsSelectedWorkflowBatch()
{
    const auto first = createBatch(QStringLiteral("B-TRACK-1"), 5);
    const auto second = createBatch(QStringLiteral("B-TRACK-2"), 7);
    QVERIFY(first.ok() && second.ok());
    window->setCurrentBatch(second.value());
    window->findChild<QListWidget *>(QStringLiteral("navigation"))->setCurrentRow(6);
    auto *tracking = window->findChild<BatchTrackingPage *>(QStringLiteral("batchTrackingPage"));
    QCOMPARE(tracking->findChild<QComboBox *>(QStringLiteral("batchSelector"))->currentData().toLongLong(), second.value());
    QCOMPARE(window->currentBatchId(), second.value());
}

void MainWindowTest::logisticsSelectsBatchFromAnyDocumentLine()
{
    const auto first = createBatch(QStringLiteral("B-MULTI-1"), 5);
    const auto second = createBatch(QStringLiteral("B-MULTI-2"), 7);
    QVERIFY(first.ok() && second.ok());
    const auto saved = logistics->create({QStringLiteral("O-MULTI"), DocumentType::Outbound,
        factoryId, externalId, QDateTime::currentDateTime(), {},
        {{first.value(), 2}, {second.value(), 3}}, personId});
    QVERIFY(saved.ok());
    window->setCurrentBatch(second.value());
    window->findChild<QListWidget *>(QStringLiteral("navigation"))->setCurrentRow(4);
    auto *page = window->findChild<LogisticsPage *>(QStringLiteral("outboundPage"));
    auto *table = page->findChild<QTableWidget *>(QStringLiteral("documents"));
    QVERIFY(table->currentRow() >= 0);
    QCOMPARE(table->item(table->currentRow(), 0)->data(Qt::UserRole + 1).toLongLong(), saved.value());
    table->clearSelection();
    table->selectRow(0);
    QCOMPARE(window->currentBatchId(), second.value());
    QVERIFY(page->findChild<QLabel *>(QStringLiteral("errorLabel"))->text().isEmpty());
}

void MainWindowTest::cancellingCurrentRecordKeepsWorkflowContext()
{
    const auto batch = createBatch(QStringLiteral("B-CANCEL"), 5);
    QVERIFY(batch.ok());
    window->setCurrentBatch(batch.value());
    window->findChild<QListWidget *>(QStringLiteral("navigation"))->setCurrentRow(2);
    auto *page = window->findChild<InboundPage *>(QStringLiteral("inboundPage"));
    QVERIFY(QMetaObject::invokeMethod(page, "cancelSelected"));
    auto *bar = window->findChild<WorkflowBar *>(QStringLiteral("workflowBar"));
    QCOMPARE(window->currentBatchId(), batch.value());
    QCOMPARE(bar->currentBatchId(), batch.value());
    QVERIFY(bar->findChild<QPushButton *>(QStringLiteral("workflowTrackingButton"))->isEnabled());
    QVERIFY(bar->findChild<QLabel *>(QStringLiteral("workflowMessageLabel"))->text().contains(QStringLiteral("恢复")));
    QVERIFY(page->findChild<QLabel *>(QStringLiteral("errorLabel"))->text().isEmpty());
}

QTEST_MAIN(MainWindowTest)
#include "tst_main_window.moc"
