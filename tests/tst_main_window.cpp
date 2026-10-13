#include "repositories/document_repository.h"
#include "repositories/inventory_repository.h"
#include "repositories/master_data_repository.h"
#include "services/inbound_service.h"
#include "services/inventory_query_service.h"
#include "services/logistics_service.h"
#include "services/master_data_service.h"
#include "test_support.h"
#include "ui/main_window.h"
#include "ui/pages/dashboard_page.h"
#include "ui/pages/inbound_page.h"
#include "ui/pages/logistics_page.h"

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTableWidget>
#include <QtTest>

class MainWindowTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void navigationHasEightEntriesAndSwitchesPages();
    void setPageOnlyReplacesPlaceholdersAndKeepsOwnership();
    void dashboardAndInboundPageReflectDocumentsAndActions();
    void transferAndOutboundPagesFilterSortAndMutate();
    void successfulMutationRefreshesDashboardAndCurrentPage();
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
    const auto category = masterService->createCategory(QStringLiteral("设备"));
    QVERIFY(category.ok());
    const auto productType = masterService->createProductType(category.value().id, QStringLiteral("GW-1"), {});
    QVERIFY(productType.ok()); productTypeId = productType.value().id;
    const auto location = masterService->createLocation(QStringLiteral("公司仓"), LocationType::Company, true);
    QVERIFY(location.ok()); locationId = location.value().id;
    window = std::make_unique<MainWindow>(*masterService, *queries, *inbound, *logistics);
    window->findChild<InboundPage *>(QStringLiteral("inboundPage"))->setConfirmationCallback([](const QString &) { return true; });
    window->findChild<LogisticsPage *>(QStringLiteral("transferPage"))->setConfirmationCallback([](const QString &) { return true; });
    window->findChild<LogisticsPage *>(QStringLiteral("outboundPage"))->setConfirmationCallback([](const QString &) { return true; });
}

void MainWindowTest::navigationHasEightEntriesAndSwitchesPages()
{
    auto *navigation = window->findChild<QListWidget *>(QStringLiteral("navigation"));
    auto *pages = window->findChild<QStackedWidget *>(QStringLiteral("pages"));
    QVERIFY(navigation); QVERIFY(pages);
    QCOMPARE(navigation->count(), 8); QCOMPARE(pages->count(), 8);
    QCOMPARE(navigation->item(0)->text(), QStringLiteral("工作台"));
    QCOMPARE(navigation->item(7)->text(), QStringLiteral("基础资料"));
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

QTEST_MAIN(MainWindowTest)
#include "tst_main_window.moc"
