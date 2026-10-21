#include "repositories/document_repository.h"
#include "repositories/inventory_repository.h"
#include "repositories/master_data_repository.h"
#include "services/batch_workflow_service.h"
#include "services/inbound_service.h"
#include "services/inventory_query_service.h"
#include "services/logistics_service.h"
#include "services/master_data_service.h"
#include "services/report_service.h"
#include "test_support.h"
#include "ui/dialogs/master_data_dialog.h"
#include "ui/main_window.h"
#include "ui/pages/batch_tracking_page.h"
#include "ui/pages/inventory_page.h"
#include "ui/pages/master_data_page.h"
#include "ui/pages/reports_page.h"

#include <QComboBox>
#include <QCheckBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>

class QueryPagesTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void inventoryShowsBalancesAndNegativeInventory();
    void batchTrackingShowsDistributionTimelineAndSnapshots();
    void batchTrackingKeepsZeroBalanceHistory();
    void reportsExportAtomicallyWithBomAndSuffix();
    void disabledMasterDataRemainsVisibleAndCanBeEnabled();
    void masterDataPagePropagatesDialogErrors();
    void locationDialogFieldsDoNotOverlap();
    void masterDataDialogsOnlyExposeRelevantFields();
    void mainWindowCreatesAllEightPages();
    void modelDialogAndPageSupportCrud();
private:
    std::unique_ptr<TestDatabase> db;
    std::unique_ptr<MasterDataRepository> repository;
    std::unique_ptr<MasterDataService> master;
    std::unique_ptr<DocumentRepository> documents;
    std::unique_ptr<InventoryRepository> inventory;
    std::unique_ptr<InboundService> inbound;
    std::unique_ptr<InventoryQueryService> queries;
    EntityId batchId=0, productTypeId=0, categoryId=0, locationId=0;
};

void QueryPagesTest::init()
{
    db=std::make_unique<TestDatabase>(); QVERIFY(db->openAndMigrate());
    repository=std::make_unique<MasterDataRepository>(db->database());
    master=std::make_unique<MasterDataService>(*repository);
    documents=std::make_unique<DocumentRepository>(db->database());
    inventory=std::make_unique<InventoryRepository>(db->database());
    inbound=std::make_unique<InboundService>(db->database(),*repository,*documents,*inventory);
    queries=std::make_unique<InventoryQueryService>(db->database());
    const auto category=master->createCategory(QStringLiteral("旧分类")); QVERIFY(category.ok()); categoryId=category.value().id;
    const auto productType=master->createProductType(categoryId,QStringLiteral("M-1"),{}); QVERIFY(productType.ok()); productTypeId=productType.value().id;
    const auto location=master->createLocation(QStringLiteral("旧仓"),LocationType::Company,true); QVERIFY(location.ok()); locationId=location.value().id;
    const auto person = master->createPersonnel(QStringLiteral("经办人"));
    QVERIFY(person.ok());
    const auto batch = inbound->create({QStringLiteral("B-1"), productTypeId, 25, locationId,
        QDateTime(QDate(2026,9,1),QTime(8,0)), {}, QStringLiteral("V40"), person.value().id});
    QVERIFY(batch.ok());
    batchId = batch.value();
}

void QueryPagesTest::inventoryShowsBalancesAndNegativeInventory()
{
    InventoryPage page(*queries); QVERIFY(page.refresh().ok());
    auto *tabs=page.findChild<QTabWidget *>(QStringLiteral("inventoryTabs")); QVERIFY(tabs); QCOMPARE(tabs->count(),4);
    QCOMPARE(page.findChild<QTableWidget *>(QStringLiteral("batchBalances"))->item(0,0)->text(),QStringLiteral("B-1"));
    QCOMPARE(page.findChild<QTableWidget *>(QStringLiteral("locationBalances"))->item(0,0)->text(),QStringLiteral("旧仓"));
    QVERIFY(page.findChild<QTableWidget *>(QStringLiteral("productTypeShipmentSummary")));
    QVERIFY(page.findChild<QLabel *>(QStringLiteral("errorLabel"))->text().isEmpty());
}

void QueryPagesTest::batchTrackingShowsDistributionTimelineAndSnapshots()
{
    BatchTrackingPage page(*queries); QVERIFY(page.refresh().ok()); QVERIFY(page.selectBatch(batchId).ok());
    auto *distribution=page.findChild<QTableWidget *>(QStringLiteral("distribution"));
    auto *timeline=page.findChild<QTableWidget *>(QStringLiteral("timeline"));
    QCOMPARE(distribution->rowCount(),1); QCOMPARE(distribution->item(0,0)->text(),QStringLiteral("旧仓"));
    QCOMPARE(timeline->rowCount(),1); QCOMPARE(timeline->item(0,4)->text(),QStringLiteral("旧仓"));
    QCOMPARE(page.findChild<QLabel *>(QStringLiteral("batchSummary"))->text(),QStringLiteral("B-1 / 旧分类 / M-1 / 物料型号：V40 / 25"));
}

void QueryPagesTest::batchTrackingKeepsZeroBalanceHistory()
{
    QVERIFY(inbound->cancel(batchId).ok());
    const auto inventoryRows = queries->batchBalances();
    QVERIFY(inventoryRows.ok());
    QVERIFY(inventoryRows.value().isEmpty());

    BatchTrackingPage page(*queries);
    QVERIFY(page.refresh().ok());
    QVERIFY(page.selectBatch(batchId).ok());
    QCOMPARE(page.findChild<QTableWidget *>(QStringLiteral("distribution"))->rowCount(), 0);
    QCOMPARE(page.findChild<QTableWidget *>(QStringLiteral("timeline"))->rowCount(), 2);
    QCOMPARE(page.findChild<QLabel *>(QStringLiteral("batchSummary"))->text(),
             QStringLiteral("B-1 / 旧分类 / M-1 / 物料型号：V40 / 0"));
}

void QueryPagesTest::reportsExportAtomicallyWithBomAndSuffix()
{
    QTemporaryDir directory; QVERIFY(directory.isValid()); const QString base=directory.filePath(QStringLiteral("report"));
    ReportService reports; ReportsPage page(*queries,reports); page.setSavePathCallback([base](QWidget *){ return base; });
    QVERIFY(page.exportCurrent()); QFile output(base+QStringLiteral(".csv")); QVERIFY(output.open(QIODevice::ReadOnly));
    QVERIFY(output.readAll().startsWith(QByteArray::fromHex("efbbbf")));
    const QString invalid=directory.filePath(QStringLiteral("missing/report.csv"));
    page.setSavePathCallback([invalid](QWidget *){ return invalid; }); QVERIFY(!page.exportCurrent()); QVERIFY(!QFile::exists(invalid));
    QVERIFY(!page.findChild<QLabel *>(QStringLiteral("errorLabel"))->text().isEmpty());
}

void QueryPagesTest::disabledMasterDataRemainsVisibleAndCanBeEnabled()
{
    QVERIFY(master->setProductTypeEnabled(productTypeId,false).ok()); MasterDataPage page(*master); QVERIFY(page.refresh().ok());
    auto *tabs=page.findChild<QTabWidget *>(QStringLiteral("masterDataTabs")); tabs->setCurrentIndex(1);
    auto *table=page.findChild<QTableWidget *>(QStringLiteral("productTypesTable")); QCOMPARE(table->rowCount(),1); QCOMPARE(table->item(0,3)->text(),QStringLiteral("停用"));
    table->selectRow(0); QVERIFY(QMetaObject::invokeMethod(&page,"toggleSelected"));
    QCOMPARE(table->item(0,3)->text(),QStringLiteral("启用"));
    const auto enabled=master->allProductTypesResult(); QVERIFY(enabled.ok()); QCOMPARE(enabled.value().size(),1);
}

void QueryPagesTest::masterDataPagePropagatesDialogErrors()
{
    MasterDataPage page(*master);
    QVERIFY(page.refresh().ok());
    QVERIFY(db->execute(QStringLiteral("PRAGMA foreign_keys=OFF")));
    QVERIFY(db->execute(QStringLiteral("DROP TABLE product_types")));

    QVERIFY(!page.refresh().ok());
    QVERIFY(!page.findChild<QLabel *>(QStringLiteral("errorLabel"))->text().isEmpty());

    TestDatabase dialogDb;
    QVERIFY(dialogDb.openAndMigrate());
    MasterDataRepository dialogRepository(dialogDb.database());
    MasterDataService dialogService(dialogRepository);
    MasterDataPage dialogPage(dialogService);
    QVERIFY(dialogPage.refresh().ok());
    auto *tabs=dialogPage.findChild<QTabWidget *>(QStringLiteral("masterDataTabs"));
    tabs->setCurrentIndex(1);
    QVERIFY(dialogDb.execute(QStringLiteral(
        "ALTER TABLE product_categories RENAME TO unavailable_categories")));
    QVERIFY(QMetaObject::invokeMethod(&dialogPage, "addSelectedType"));
    QVERIFY(!dialogPage.findChild<QLabel *>(QStringLiteral("errorLabel"))->text().isEmpty());
}

void QueryPagesTest::locationDialogFieldsDoNotOverlap()
{
    MasterDataDialog dialog(*master, MasterDataDialog::Entity::Location);
    QVERIFY(dialog.initialize().ok());
    dialog.show();
    QCoreApplication::processEvents();
    auto *type=dialog.findChild<QComboBox *>(QStringLiteral("locationType"));
    auto *inventoryFlag=dialog.findChild<QCheckBox *>(QStringLiteral("countsAsInventory"));
    QVERIFY(type);
    QVERIFY(inventoryFlag);
    QVERIFY(!type->geometry().intersects(inventoryFlag->geometry()));
    QVERIFY(inventoryFlag->y() >= type->geometry().bottom());
}

void QueryPagesTest::masterDataDialogsOnlyExposeRelevantFields()
{
    MasterDataDialog category(*master, MasterDataDialog::Entity::Category);
    QVERIFY(category.findChild<QLineEdit *>(QStringLiteral("name")));
    QVERIFY(!category.findChild<QLineEdit *>(QStringLiteral("code")));
    QVERIFY(!category.findChild<QComboBox *>(QStringLiteral("categoryId")));
    QVERIFY(!category.findChild<QComboBox *>(QStringLiteral("locationType")));
    QVERIFY(!category.findChild<QCheckBox *>(QStringLiteral("countsAsInventory")));

    MasterDataDialog productType(*master, MasterDataDialog::Entity::ProductType);
    QVERIFY(productType.findChild<QLineEdit *>(QStringLiteral("code")));
    QVERIFY(productType.findChild<QLineEdit *>(QStringLiteral("details")));
    QVERIFY(productType.findChild<QComboBox *>(QStringLiteral("categoryId")));
    QVERIFY(!productType.findChild<QComboBox *>(QStringLiteral("locationType")));
    QVERIFY(!productType.findChild<QCheckBox *>(QStringLiteral("countsAsInventory")));

    MasterDataDialog location(*master, MasterDataDialog::Entity::Location);
    QVERIFY(location.findChild<QLineEdit *>(QStringLiteral("name")));
    QVERIFY(location.findChild<QComboBox *>(QStringLiteral("locationType")));
    QVERIFY(location.findChild<QCheckBox *>(QStringLiteral("countsAsInventory")));
    QVERIFY(!location.findChild<QLineEdit *>(QStringLiteral("code")));
    QVERIFY(!location.findChild<QLineEdit *>(QStringLiteral("details")));
    QVERIFY(!location.findChild<QComboBox *>(QStringLiteral("categoryId")));
}

void QueryPagesTest::mainWindowCreatesAllEightPages()
{
    LogisticsService logistics(db->database(),*repository,*documents,*inventory);
    BatchWorkflowService workflow(*master, *documents, *inventory, *queries);
    MainWindow window(*master,*queries,*inbound,logistics,workflow);
    auto *pages=window.findChild<QStackedWidget *>(QStringLiteral("pages"));
    QVERIFY(qobject_cast<InventoryPage *>(pages->widget(4))); QVERIFY(qobject_cast<BatchTrackingPage *>(pages->widget(5)));
    QVERIFY(qobject_cast<ReportsPage *>(pages->widget(6))); QVERIFY(qobject_cast<MasterDataPage *>(pages->widget(7)));
}

void QueryPagesTest::modelDialogAndPageSupportCrud()
{
    MasterDataDialog create(*master, MasterDataDialog::Entity::ProductModel);
    QVERIFY(create.initialize().ok());
    auto *type = create.findChild<QComboBox *>(QStringLiteral("productTypeId"));
    QVERIFY(type);
    type->setCurrentIndex(type->findData(productTypeId));
    create.findChild<QLineEdit *>(QStringLiteral("code"))->setText(QStringLiteral("F25"));
    QVERIFY(create.save());
    const auto all = master->allProductModelsResult();
    QVERIFY(all.ok());
    QCOMPARE(all.value().size(), 1);
    MasterDataDialog edit(*master, MasterDataDialog::Entity::ProductModel);
    QVERIFY(edit.initialize(all.value().front().id).ok());
    edit.findChild<QLineEdit *>(QStringLiteral("code"))->setText(QStringLiteral("P25"));
    QVERIFY(edit.save());
    MasterDataPage page(*master);
    auto *tabs = page.findChild<QTabWidget *>(QStringLiteral("masterDataTabs"));
    QCOMPARE(tabs->count(), 5);
    tabs->setCurrentIndex(3);
    auto *models = page.findChild<QTableWidget *>(QStringLiteral("productModelsTable"));
    QVERIFY(models);
    QCOMPARE(models->item(0, 0)->text(), QStringLiteral("P25"));
    QCOMPARE(models->item(0, 1)->text(), QStringLiteral("M-1"));
    models->selectRow(0);
    QVERIFY(QMetaObject::invokeMethod(&page, "toggleSelected"));
    QCOMPARE(models->item(0, 3)->text(), QStringLiteral("停用"));
    page.setConfirmationCallback([](const QString &) { return false; });
    models->selectRow(0);
    QVERIFY(QMetaObject::invokeMethod(&page, "deleteSelected"));
    QCOMPARE(models->rowCount(), 1);
    page.setConfirmationCallback([](const QString &) { return true; });
    QVERIFY(QMetaObject::invokeMethod(&page, "deleteSelected"));
    QCOMPARE(models->rowCount(), 0);
    QVERIFY(master->allProductModelsResult().value().isEmpty());
}

QTEST_MAIN(QueryPagesTest)
#include "tst_query_pages.moc"
