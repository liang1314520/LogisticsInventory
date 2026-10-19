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
#include "ui/dialogs/logistics_dialog.h"
#include "ui/pages/logistics_page.h"
#include "ui/widgets/workflow_bar.h"

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
    void outboundCreateShowsInboundPrerequisite();
    void returnPageShowsNoReturnableQuantity();
    void historyViewRemainsAvailableWhenWorkflowBlocksNewAction();
    void workflowContextAppearsOnTrackingAndMasterDataPages();
    void trackingPageKeepsSelectedWorkflowBatch();
    void logisticsSelectsBatchFromAnyDocumentLine();
    void deletingCurrentDraftClearsWorkflowContext();
private:
    std::unique_ptr<TestDatabase> db;
    std::unique_ptr<MasterDataRepository> masterRepository;
    std::unique_ptr<MasterDataService> masterService;
    std::unique_ptr<DocumentRepository> documents;
    std::unique_ptr<InventoryRepository> inventory;
    std::unique_ptr<InboundService> inbound;
    std::unique_ptr<LogisticsService> logistics;
    std::unique_ptr<InventoryQueryService> queries;
    std::unique_ptr<MainWindow> window;
    EntityId productTypeId = 0;
    EntityId locationId = 0;
    std::unique_ptr<BatchWorkflowService> workflow;
};

void MainWindowTest::init()
{
    db = std::make_unique<TestDatabase>();
    QVERIFY(db->openAndMigrate());
    masterRepository = std::make_unique<MasterDataRepository>(db->database());
    masterService = std::make_unique<MasterDataService>(*masterRepository);
    documents = std::make_unique<DocumentRepository>(db->database());
    inventory = std::make_unique<InventoryRepository>(db->database());
    inbound = std::make_unique<InboundService>(db->database(), *masterRepository, *documents, *inventory);
    logistics = std::make_unique<LogisticsService>(db->database(), *masterRepository, *documents, *inventory);
    queries = std::make_unique<InventoryQueryService>(db->database());
    workflow = std::make_unique<BatchWorkflowService>(*masterService, *documents,
                                                       *inventory, *queries);
    const auto category = masterService->createCategory(QStringLiteral("设备"));
    QVERIFY(category.ok());
    const auto productType = masterService->createProductType(category.value().id, QStringLiteral("GW-1"), {});
    QVERIFY(productType.ok()); productTypeId = productType.value().id;
    const auto location = masterService->createLocation(QStringLiteral("公司仓"), LocationType::Company, true);
    QVERIFY(location.ok()); locationId = location.value().id;
    window = std::make_unique<MainWindow>(*masterService, *queries, *inbound, *logistics,
                                           *workflow);
    window->findChild<InboundPage *>(QStringLiteral("inboundPage"))->setConfirmationCallback([](const QString &) { return true; });
    window->findChild<LogisticsPage *>(QStringLiteral("transferPage"))->setConfirmationCallback([](const QString &) { return true; });
    window->findChild<LogisticsPage *>(QStringLiteral("outboundPage"))->setConfirmationCallback([](const QString &) { return true; });
}

void MainWindowTest::navigationHasNineEntriesAndSwitchesPages()
{
    auto *navigation = window->findChild<QListWidget *>(QStringLiteral("navigation"));
    auto *pages = window->findChild<QStackedWidget *>(QStringLiteral("pages"));
    QVERIFY(navigation); QVERIFY(pages);
    QCOMPARE(navigation->count(), 9); QCOMPARE(pages->count(), 9);
    QCOMPARE(navigation->item(0)->text(), QStringLiteral("工作台"));
    QCOMPARE(navigation->item(7)->text(), QStringLiteral("基础资料"));
    QCOMPARE(navigation->item(8)->text(), QStringLiteral("退回管理"));
    navigation->setCurrentRow(8);
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("returnPage"));
    navigation->setCurrentRow(3);
    QCOMPARE(pages->currentIndex(), 3);
}

void MainWindowTest::setPageOnlyReplacesPlaceholdersAndKeepsOwnership()
{
    auto *pages = window->findChild<QStackedWidget *>(QStringLiteral("pages"));
    auto *dashboard = window->findChild<DashboardPage *>(QStringLiteral("dashboardPage"));
    QVERIFY(pages); QVERIFY(dashboard);
    QVector<QWidget *> rejectedPages;
    for (int index = 0; index < 4; ++index) {
        QWidget *corePage = pages->widget(index);
        auto *rejected = new QWidget(window.get());
        rejectedPages.append(rejected);
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
    InboundDraft oldDraft{QStringLiteral("B-OLD"), productTypeId, 10, locationId,
        QDateTime::fromString(QStringLiteral("2026-08-01T08:00:00"), Qt::ISODate), {}};
    InboundDraft recentDraft{QStringLiteral("B-NEW"), productTypeId, 25, locationId,
        QDateTime::currentDateTime(), {}};
    const auto oldId = inbound->createDraft(oldDraft); const auto recentId = inbound->createDraft(recentDraft);
    QVERIFY(oldId.ok()); QVERIFY(recentId.ok()); QVERIFY(inbound->confirm(recentId.value()).ok());
    auto *dashboard = window->findChild<DashboardPage *>(QStringLiteral("dashboardPage"));
    auto *page = window->findChild<InboundPage *>(QStringLiteral("inboundPage"));
    QVERIFY(dashboard); QVERIFY(page); QVERIFY(dashboard->refresh().ok()); QVERIFY(page->refresh().ok());
    QCOMPARE(dashboard->findChild<QLabel *>(QStringLiteral("totalInventory"))->text(), QStringLiteral("25"));
    auto *table = page->findChild<QTableWidget *>(QStringLiteral("documents"));
    QCOMPARE(table->rowCount(), 2); QCOMPARE(table->item(0, 1)->text(), QStringLiteral("B-NEW"));
    table->selectRow(0); QCoreApplication::processEvents();
    QVERIFY(page->findChild<QPushButton *>(QStringLiteral("viewButton"))->isEnabled());
    QVERIFY(page->findChild<QPushButton *>(QStringLiteral("cancelButton"))->isEnabled());
    QVERIFY(!page->findChild<QPushButton *>(QStringLiteral("editButton"))->isEnabled());
    table->selectRow(1); QCoreApplication::processEvents();
    QVERIFY(page->findChild<QPushButton *>(QStringLiteral("editButton"))->isEnabled());
    QVERIFY(page->findChild<QPushButton *>(QStringLiteral("deleteButton"))->isEnabled());
}

void MainWindowTest::transferAndOutboundPagesFilterSortAndMutate()
{
    InboundDraft stock{QStringLiteral("B-STOCK"), productTypeId, 500, locationId,
        QDateTime::fromString(QStringLiteral("2026-07-01T08:00:00"), Qt::ISODate), {}};
    const auto batch = inbound->createDraft(stock); QVERIFY(batch.ok()); QVERIFY(inbound->confirm(batch.value()).ok());
    const auto destination = masterService->createLocation(QStringLiteral("项目仓"), LocationType::ProjectSite, true);
    const auto customer = masterService->createLocation(QStringLiteral("客户"), LocationType::Customer, false);
    QVERIFY(destination.ok()); QVERIFY(customer.ok());
    const auto makeDraft = [&](const QString &number, DocumentType type, EntityId target, const QString &date) {
        return LogisticsDraft{number, type, locationId, target,
            QDateTime::fromString(date, Qt::ISODate), {}, {{batch.value(), 20}}};
    };
    QVector<EntityId> transferIds; QVector<EntityId> outboundIds;
    for (int index = 0; index < 3; ++index) {
        const QString date = QStringLiteral("2026-0%1-01T10:00:00").arg(index + 7);
        auto transfer = logistics->createDraft(makeDraft(QStringLiteral("T-%1").arg(index), DocumentType::Transfer, destination.value().id, date));
        auto outbound = logistics->createDraft(makeDraft(QStringLiteral("O-%1").arg(index), DocumentType::Outbound, customer.value().id, date));
        QVERIFY(transfer.ok()); QVERIFY(outbound.ok()); transferIds.append(transfer.value()); outboundIds.append(outbound.value());
        if (index > 0) { QVERIFY(logistics->confirm(transfer.value()).ok()); QVERIFY(logistics->confirm(outbound.value()).ok()); }
        if (index == 2) { QVERIFY(logistics->cancel(transfer.value()).ok()); QVERIFY(logistics->cancel(outbound.value()).ok()); }
    }
    auto *navigation = window->findChild<QListWidget *>(QStringLiteral("navigation"));
    auto verifyPage = [&](int pageIndex, const QString &prefix, LogisticsPage *&page) {
        navigation->setCurrentRow(pageIndex);
        auto *pages = window->findChild<QStackedWidget *>(QStringLiteral("pages"));
        page = qobject_cast<LogisticsPage *>(pages->currentWidget());
        QVERIFY(page);
        auto *table = page->findChild<QTableWidget *>(QStringLiteral("documents"));
        QCOMPARE(table->rowCount(), 3); QCOMPARE(table->item(0, 1)->text(), prefix + QStringLiteral("-2"));
        QCOMPARE(table->item(1, 1)->text(), prefix + QStringLiteral("-1")); QCOMPARE(table->item(2, 1)->text(), prefix + QStringLiteral("-0"));
        table->selectRow(2); QCoreApplication::processEvents(); QVERIFY(page->findChild<QPushButton *>(QStringLiteral("editButton"))->isEnabled());
        table->selectRow(1); QCoreApplication::processEvents(); QVERIFY(page->findChild<QPushButton *>(QStringLiteral("cancelButton"))->isEnabled());
        QVERIFY(page->findChild<QPushButton *>(QStringLiteral("viewButton"))->isEnabled());
        table->selectRow(0); QCoreApplication::processEvents(); QVERIFY(page->findChild<QPushButton *>(QStringLiteral("copyButton"))->isEnabled());
    };
    LogisticsPage *transferPage = nullptr;
    verifyPage(2, QStringLiteral("T"), transferPage);
    QSignalSpy transferChanged(transferPage, &LogisticsPage::dataChanged);
    auto *transferTable = transferPage->findChild<QTableWidget *>(QStringLiteral("documents")); transferTable->selectRow(1);
    QVERIFY(QMetaObject::invokeMethod(transferPage, "cancelSelected")); QCOMPARE(transferChanged.count(), 1); QCOMPARE(transferTable->item(1, 3)->text(), QStringLiteral("已取消"));
    transferTable->selectRow(1);
    QVERIFY(QMetaObject::invokeMethod(transferPage, "copySelected")); QCOMPARE(transferChanged.count(), 2); QCOMPARE(transferTable->rowCount(), 4);
    LogisticsPage *outboundPage = nullptr;
    verifyPage(3, QStringLiteral("O"), outboundPage);
    QSignalSpy outboundChanged(outboundPage, &LogisticsPage::dataChanged);
    auto *outboundTable = outboundPage->findChild<QTableWidget *>(QStringLiteral("documents")); outboundTable->selectRow(1);
    QVERIFY(QMetaObject::invokeMethod(outboundPage, "cancelSelected")); QCOMPARE(outboundChanged.count(), 1); QCOMPARE(outboundTable->item(1, 3)->text(), QStringLiteral("已取消"));
    outboundTable->selectRow(1);
    QVERIFY(QMetaObject::invokeMethod(outboundPage, "copySelected")); QCOMPARE(outboundChanged.count(), 2); QCOMPARE(outboundTable->rowCount(), 4);
    auto *dashboard = window->findChild<DashboardPage *>(QStringLiteral("dashboardPage")); QCOMPARE(dashboard->findChild<QLabel *>(QStringLiteral("totalInventory"))->text(), QStringLiteral("500"));
}

void MainWindowTest::successfulMutationRefreshesDashboardAndCurrentPage()
{
    InboundDraft draft{QStringLiteral("B-CANCEL"), productTypeId, 30, locationId,
        QDateTime::currentDateTime(), {}};
    const auto id = inbound->createDraft(draft); QVERIFY(id.ok()); QVERIFY(inbound->confirm(id.value()).ok());
    auto *navigation = window->findChild<QListWidget *>(QStringLiteral("navigation"));
    navigation->setCurrentRow(1);
    auto *page = window->findChild<InboundPage *>(QStringLiteral("inboundPage"));
    auto *table = page->findChild<QTableWidget *>(QStringLiteral("documents"));
    const int cancelledRow = [&] {
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->item(row, 1)->text() == QStringLiteral("B-CANCEL")) return row;
        return -1;
    }();
    QVERIFY(cancelledRow >= 0); table->selectRow(cancelledRow); QSignalSpy changed(page, &InboundPage::dataChanged);
    QVERIFY(QMetaObject::invokeMethod(page, "cancelSelected"));
    QCOMPARE(changed.count(), 1); QCOMPARE(table->item(cancelledRow, 3)->text(), QStringLiteral("已取消"));
    auto *dashboard = window->findChild<DashboardPage *>(QStringLiteral("dashboardPage"));
    QCOMPARE(dashboard->findChild<QLabel *>(QStringLiteral("totalInventory"))->text(), QStringLiteral("0"));
    table->selectRow(cancelledRow); QVERIFY(QMetaObject::invokeMethod(page, "copySelected"));
    QCOMPARE(changed.count(), 2); QCOMPARE(table->rowCount(), 2);
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
    const auto created = inbound->createDraft({QStringLiteral("B-WORKFLOW"), productTypeId, 12,
        locationId, QDateTime::currentDateTime(), {}});
    QVERIFY(created.ok());
    QVERIFY(inbound->confirm(created.value()).ok());
    auto *bar = window->findChild<WorkflowBar *>(QStringLiteral("workflowBar"));
    QVERIFY(bar);
    QVERIFY(bar->refresh().ok());
    auto *selector = bar->findChild<QComboBox *>(QStringLiteral("workflowBatchSelector"));
    QVERIFY(selector);
    selector->setCurrentIndex(selector->findData(created.value()));
    QCoreApplication::processEvents();
    QCOMPARE(window->currentBatchId(), created.value());
    QCOMPARE(bar->currentBatchId(), created.value());
}

void MainWindowTest::mutationRefreshesWorkflowState()
{
    const auto created = inbound->createDraft({QStringLiteral("B-MUTATION"), productTypeId, 12,
        locationId, QDateTime::currentDateTime(), {}});
    QVERIFY(created.ok());
    window->setCurrentBatch(created.value());
    auto *bar = window->findChild<WorkflowBar *>(QStringLiteral("workflowBar"));
    QVERIFY(bar);
    QVERIFY(bar->refresh().ok());
    QVERIFY(inbound->confirm(created.value()).ok());
    QVERIFY(QMetaObject::invokeMethod(window.get(), "refreshAfterMutation"));
    QVERIFY(bar->findChild<QLabel *>(QStringLiteral("workflowStatusLabel"))->text()
                .contains(QStringLiteral("进行中")));
    QVERIFY(bar->findChild<QLabel *>(QStringLiteral("workflowMessageLabel"))->text()
                .contains(QStringLiteral("可直接出库")));
    auto *page = window->findChild<InboundPage *>(QStringLiteral("inboundPage"));
    QVERIFY(!page->findChild<QLabel *>(QStringLiteral("workflowContext"))->text()
                 .contains(QStringLiteral("先确认入库")));
}

void MainWindowTest::nextActionNavigatesToExpectedPage()
{
    const auto created = inbound->createDraft({QStringLiteral("B-NEXT"), productTypeId, 12,
        locationId, QDateTime::currentDateTime(), {}});
    QVERIFY(created.ok());
    QVERIFY(inbound->confirm(created.value()).ok());
    window->setCurrentBatch(created.value());
    auto *bar = window->findChild<WorkflowBar *>(QStringLiteral("workflowBar"));
    QVERIFY(bar);
    QVERIFY(bar->refresh().ok());
    auto *next = bar->findChild<QPushButton *>(QStringLiteral("workflowNextButton"));
    QVERIFY(next->isEnabled());
    next->click();
    auto *pages = window->findChild<QStackedWidget *>(QStringLiteral("pages"));
    QCOMPARE(pages->currentIndex(), 3);
}

void MainWindowTest::inboundPageSelectsWorkflowBatch()
{
    const auto first = inbound->createDraft({QStringLiteral("B-SELECT"), productTypeId, 12,
        locationId, QDateTime::currentDateTime(), {}});
    const auto second = inbound->createDraft({QStringLiteral("B-OTHER"), productTypeId, 9,
        locationId, QDateTime::currentDateTime(), {}});
    QVERIFY(first.ok() && second.ok());
    window->setCurrentBatch(first.value());
    window->findChild<QListWidget *>(QStringLiteral("navigation"))->setCurrentRow(1);
    auto *page = window->findChild<InboundPage *>(QStringLiteral("inboundPage"));
    auto *table = page->findChild<QTableWidget *>(QStringLiteral("documents"));
    QVERIFY(table->currentRow() >= 0);
    QCOMPARE(table->item(table->currentRow(), 0)->text().toLongLong(), first.value());
    auto *context = page->findChild<QLabel *>(QStringLiteral("workflowContext"));
    QVERIFY(context);
    QVERIFY(context->text().contains(QStringLiteral("步骤 02 / 06")));
    QVERIFY(context->text().contains(QStringLiteral("先确认入库")));
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 0)->text().toLongLong() == second.value())
            table->selectRow(row);
    }
    QCOMPARE(window->currentBatchId(), second.value());
}

void MainWindowTest::logisticsPageSelectsWorkflowBatch()
{
    const auto batch = inbound->createDraft({QStringLiteral("B-LOGISTICS"), productTypeId, 12,
        locationId, QDateTime::currentDateTime(), {}});
    QVERIFY(batch.ok());
    QVERIFY(inbound->confirm(batch.value()).ok());
    const auto target = masterService->createLocation(QStringLiteral("工厂"), LocationType::Factory, true);
    QVERIFY(target.ok());
    const auto draft = logistics->createDraft({QStringLiteral("T-SELECT"), DocumentType::Transfer,
        locationId, target.value().id, QDateTime::currentDateTime().addSecs(-60), {}, {{batch.value(), 2}}});
    const auto history = logistics->createDraft({QStringLiteral("T-HISTORY"), DocumentType::Transfer,
        locationId, target.value().id, QDateTime::currentDateTime(), {}, {{batch.value(), 1}}});
    QVERIFY(draft.ok() && history.ok());
    QVERIFY(logistics->confirm(history.value()).ok());
    window->setCurrentBatch(batch.value());
    window->findChild<WorkflowBar *>(QStringLiteral("workflowBar"))
        ->findChild<QPushButton *>(QStringLiteral("workflowNextButton"))->click();
    auto *page = window->findChild<LogisticsPage *>(QStringLiteral("transferPage"));
    auto *table = page->findChild<QTableWidget *>(QStringLiteral("documents"));
    QCOMPARE(window->findChild<QStackedWidget *>(QStringLiteral("pages"))->currentIndex(), 2);
    QVERIFY(table->currentRow() >= 0);
    QCOMPARE(table->item(table->currentRow(), 0)->text().toLongLong(), draft.value());
    QVERIFY(page->findChild<QPushButton *>(QStringLiteral("editButton"))->isEnabled());
}

void MainWindowTest::outboundCreateShowsInboundPrerequisite()
{
    const auto batch = inbound->createDraft({QStringLiteral("B-BLOCKED"), productTypeId, 12,
        locationId, QDateTime::currentDateTime(), {}});
    QVERIFY(batch.ok());
    window->setCurrentBatch(batch.value());
    window->findChild<QListWidget *>(QStringLiteral("navigation"))->setCurrentRow(3);
    auto *page = window->findChild<LogisticsPage *>(QStringLiteral("outboundPage"));
    QTimer::singleShot(0, page, [page] {
        if (auto *dialog = page->findChild<LogisticsDialog *>()) dialog->reject();
    });
    const auto result = page->openCreateDialog();
    QVERIFY(!result.ok());
    QVERIFY(result.error().message.contains(QStringLiteral("先确认入库")));
    QCOMPARE(page->findChild<QLabel *>(QStringLiteral("errorLabel"))->text(), result.error().message);
    const auto listed = logistics->list(DocumentType::Outbound);
    QVERIFY(listed.ok());
    QVERIFY(listed.value().isEmpty());
}

void MainWindowTest::returnPageShowsNoReturnableQuantity()
{
    const auto batch = inbound->createDraft({QStringLiteral("B-NO-RETURN"), productTypeId, 12,
        locationId, QDateTime::currentDateTime(), {}});
    QVERIFY(batch.ok());
    QVERIFY(inbound->confirm(batch.value()).ok());
    window->setCurrentBatch(batch.value());
    window->findChild<QListWidget *>(QStringLiteral("navigation"))->setCurrentRow(8);
    auto *page = window->findChild<LogisticsPage *>(QStringLiteral("returnPage"));
    QTimer::singleShot(0, page, [page] {
        if (auto *dialog = page->findChild<LogisticsDialog *>()) dialog->reject();
    });
    const auto result = page->openCreateDialog();
    QVERIFY(!result.ok());
    QVERIFY(result.error().message.contains(QStringLiteral("可退")));
}

void MainWindowTest::historyViewRemainsAvailableWhenWorkflowBlocksNewAction()
{
    const auto batch = inbound->createDraft({QStringLiteral("B-HISTORY"), productTypeId, 12,
        locationId, QDateTime::currentDateTime(), {}});
    const auto customer = masterService->createLocation(QStringLiteral("客户"), LocationType::Customer, false);
    QVERIFY(batch.ok() && customer.ok());
    QVERIFY(inbound->confirm(batch.value()).ok());
    const auto outbound = logistics->createDraft({QStringLiteral("O-HISTORY"), DocumentType::Outbound,
        locationId, customer.value().id, QDateTime::currentDateTime(), {}, {{batch.value(), 12}}});
    QVERIFY(outbound.ok());
    QVERIFY(logistics->confirm(outbound.value()).ok());
    window->setCurrentBatch(batch.value());
    window->findChild<QListWidget *>(QStringLiteral("navigation"))->setCurrentRow(3);
    auto *page = window->findChild<LogisticsPage *>(QStringLiteral("outboundPage"));
    QTimer::singleShot(0, page, [page] {
        if (auto *dialog = page->findChild<LogisticsDialog *>()) dialog->reject();
    });
    QVERIFY(!page->openCreateDialog().ok());
    QVERIFY(page->findChild<QPushButton *>(QStringLiteral("viewButton"))->isEnabled());
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
    const auto batch = inbound->createDraft({QStringLiteral("B-CONTEXT"), productTypeId, 12,
        locationId, QDateTime::currentDateTime(), {}});
    QVERIFY(batch.ok());
    QVERIFY(inbound->confirm(batch.value()).ok());
    window->setCurrentBatch(batch.value());
    auto *navigation = window->findChild<QListWidget *>(QStringLiteral("navigation"));
    navigation->setCurrentRow(5);
    auto *tracking = window->findChild<BatchTrackingPage *>(QStringLiteral("batchTrackingPage"));
    auto *trackingContext = tracking->findChild<QLabel *>(QStringLiteral("workflowContext"));
    QVERIFY(trackingContext);
    QVERIFY(trackingContext->text().contains(QStringLiteral("步骤 06 / 06")));
    navigation->setCurrentRow(7);
    auto *masterPage = window->findChild<MasterDataPage *>(QStringLiteral("masterDataPage"));
    auto *masterContext = masterPage->findChild<QLabel *>(QStringLiteral("workflowContext"));
    QVERIFY(masterContext);
    QVERIFY(masterContext->text().contains(QStringLiteral("步骤 01 / 06")));
}

void MainWindowTest::trackingPageKeepsSelectedWorkflowBatch()
{
    const auto first = inbound->createDraft({QStringLiteral("B-TRACK-1"), productTypeId, 5,
        locationId, QDateTime::currentDateTime(), {}});
    const auto second = inbound->createDraft({QStringLiteral("B-TRACK-2"), productTypeId, 7,
        locationId, QDateTime::currentDateTime(), {}});
    QVERIFY(first.ok() && second.ok());
    QVERIFY(inbound->confirm(first.value()).ok());
    QVERIFY(inbound->confirm(second.value()).ok());
    window->setCurrentBatch(second.value());
    window->findChild<QListWidget *>(QStringLiteral("navigation"))->setCurrentRow(5);
    auto *tracking = window->findChild<BatchTrackingPage *>(QStringLiteral("batchTrackingPage"));
    QCOMPARE(tracking->findChild<QComboBox *>(QStringLiteral("batchSelector"))->currentData().toLongLong(), second.value());
    QCOMPARE(window->currentBatchId(), second.value());
}

void MainWindowTest::logisticsSelectsBatchFromAnyDocumentLine()
{
    const auto first = inbound->createDraft({QStringLiteral("B-MULTI-1"), productTypeId, 5,
        locationId, QDateTime::currentDateTime(), {}});
    const auto second = inbound->createDraft({QStringLiteral("B-MULTI-2"), productTypeId, 7,
        locationId, QDateTime::currentDateTime(), {}});
    const auto customer = masterService->createLocation(QStringLiteral("客户"), LocationType::Customer, false);
    QVERIFY(first.ok() && second.ok() && customer.ok());
    QVERIFY(inbound->confirm(first.value()).ok());
    QVERIFY(inbound->confirm(second.value()).ok());
    const auto draft = logistics->createDraft({QStringLiteral("O-MULTI"), DocumentType::Outbound,
        locationId, customer.value().id, QDateTime::currentDateTime(), {},
        {{first.value(), 2}, {second.value(), 3}}});
    QVERIFY(draft.ok());
    window->setCurrentBatch(second.value());
    window->findChild<QListWidget *>(QStringLiteral("navigation"))->setCurrentRow(3);
    auto *page = window->findChild<LogisticsPage *>(QStringLiteral("outboundPage"));
    auto *table = page->findChild<QTableWidget *>(QStringLiteral("documents"));
    QVERIFY(table->currentRow() >= 0);
    QCOMPARE(table->item(table->currentRow(), 0)->text().toLongLong(), draft.value());
    table->clearSelection();
    table->selectRow(0);
    QCOMPARE(window->currentBatchId(), second.value());
    QVERIFY(page->findChild<QLabel *>(QStringLiteral("errorLabel"))->text().isEmpty());
}

void MainWindowTest::deletingCurrentDraftClearsWorkflowContext()
{
    const auto batch = inbound->createDraft({QStringLiteral("B-DELETE"), productTypeId, 5,
        locationId, QDateTime::currentDateTime(), {}});
    QVERIFY(batch.ok());
    window->setCurrentBatch(batch.value());
    window->findChild<QListWidget *>(QStringLiteral("navigation"))->setCurrentRow(1);
    auto *page = window->findChild<InboundPage *>(QStringLiteral("inboundPage"));
    QVERIFY(QMetaObject::invokeMethod(page, "deleteSelected"));
    auto *bar = window->findChild<WorkflowBar *>(QStringLiteral("workflowBar"));
    QCOMPARE(window->currentBatchId(), EntityId(0));
    QCOMPARE(bar->currentBatchId(), EntityId(0));
    QVERIFY(!bar->findChild<QPushButton *>(QStringLiteral("workflowTrackingButton"))->isEnabled());
    QVERIFY(page->findChild<QLabel *>(QStringLiteral("errorLabel"))->text().isEmpty());
}

QTEST_MAIN(MainWindowTest)
#include "tst_main_window.moc"
