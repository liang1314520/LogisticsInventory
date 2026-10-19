#include "repositories/document_repository.h"
#include "repositories/inventory_repository.h"
#include "repositories/master_data_repository.h"
#include "services/inbound_service.h"
#include "services/inventory_query_service.h"
#include "services/logistics_service.h"
#include "services/master_data_service.h"
#include "test_support.h"
#include "ui/dialogs/inbound_dialog.h"
#include "ui/dialogs/logistics_dialog.h"
#include "ui/pages/inbound_page.h"
#include "ui/pages/logistics_page.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTimer>
#include <QtTest>

#include <memory>
#include <algorithm>

class DocumentDialogsTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void inboundFiltersDisabledAndAcceptsQint64();
    void inboundCreatesAndUpdatesDraft();
    void inboundFocusesInvalidFieldAndLoadsDisabledReference();
    void newDialogsFilterDisabledLocations();
    void logisticsFiltersBatchesAndDestinations();
    void transferSourceChangeDropsMatchingDestinationHistory();
    void transferSourceChangeDropsUnavailableLineBatch();
    void sourceQueryFailureClearsStaleLineChoices();
    void deletingEarlierLineKeepsCascadeBoundToCurrentRow();
    void newLineLoadsDefaultBatch();
    void logisticsDialogPreselectsPreferredBatch();
    void pageRequestSignalsEmitOnce();
    void logisticsCreatesAndUpdatesDraft();
    void logisticsRejectsEditingNonDrafts();
    void logisticsLoadsDisabledHistoricalLocations();
    void logisticsLoadsZeroBalanceHistoricalLineReadOnly();
    void logisticsFocusesExactLineField();
    void logisticsQueryErrorsPropagate();
    void logisticsDestinationQueryErrorsPropagate();
    void logisticsPreservesMultipleLinesAndReadOnlyState();
    void shortageContainsBusinessDetailsAndExactField();
    void inboundPageExposesDialogInitializeFailures_data();
    void inboundPageExposesDialogInitializeFailures();
    void logisticsPageExposesDialogInitializeFailures_data();
    void logisticsPageExposesDialogInitializeFailures();
    void pagesExposeFailuresAndClearErrorsOnSuccess();
    void logisticsPageConfirmationAndDataChanged();
    void pageConfirmationAndDataChanged();
    void outboundModelCascadeAndZeroStockReturn();
private:
    EntityId addLocation(const QString &name, bool inventory,
                         LocationType type = LocationType::Company);
    EntityId addConfirmedBatch(const QString &batchNo, qint64 quantity);
    std::unique_ptr<TestDatabase> db;
    std::unique_ptr<MasterDataRepository> masterRepository;
    std::unique_ptr<MasterDataService> master;
    std::unique_ptr<DocumentRepository> documents;
    std::unique_ptr<InventoryRepository> inventory;
    std::unique_ptr<InboundService> inbound;
    std::unique_ptr<LogisticsService> logistics;
    std::unique_ptr<InventoryQueryService> queries;
    EntityId productTypeId = 0;
    EntityId categoryId = 0;
    EntityId sourceId = 0;
    EntityId destinationId = 0;
    EntityId externalId = 0;
    EntityId batchId = 0;
};

void DocumentDialogsTest::init()
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
    const auto category = master->createCategory(QStringLiteral("设备")); QVERIFY(category.ok());
    categoryId = category.value().id;
    const auto productType = master->createProductType(categoryId, QStringLiteral("GW"), {});
    QVERIFY(productType.ok()); productTypeId = productType.value().id;
    sourceId = addLocation(QStringLiteral("公司仓"), true);
    destinationId = addLocation(QStringLiteral("项目仓"), true, LocationType::ProjectSite);
    externalId = addLocation(QStringLiteral("客户"), false, LocationType::Customer);
    const auto created = inbound->createDraft({QStringLiteral("B-1"), productTypeId, 5000000000LL,
        sourceId, QDateTime::currentDateTime(), {}});
    QVERIFY(created.ok()); batchId = created.value(); QVERIFY(inbound->confirm(batchId).ok());
}

EntityId DocumentDialogsTest::addLocation(const QString &name, bool inventory, LocationType type)
{
    const auto result = master->createLocation(name, type, inventory);
    return result.ok() ? result.value().id : 0;
}

EntityId DocumentDialogsTest::addConfirmedBatch(const QString &batchNo, qint64 quantity)
{
    const auto created = inbound->createDraft({batchNo, productTypeId, quantity, sourceId,
        QDateTime::currentDateTime(), {}});
    if (!created.ok() || !inbound->confirm(created.value()).ok())
        return 0;
    return created.value();
}

void DocumentDialogsTest::inboundFiltersDisabledAndAcceptsQint64()
{
    const auto disabled = master->createProductType(categoryId, QStringLiteral("OFF"), {});
    QVERIFY(disabled.ok()); QVERIFY(master->setProductTypeEnabled(disabled.value().id, false).ok());
    InboundDialog dialog(*master, *inbound);
    QVERIFY(dialog.initialize().ok());
    auto *productTypes = dialog.findChild<QComboBox *>(QStringLiteral("productTypeId"));
    QVERIFY(productTypes); QCOMPARE(productTypes->findData(disabled.value().id), -1);
    dialog.setDraft({QStringLiteral("B-LARGE"), productTypeId, 5000000000LL,
                     sourceId, QDateTime::currentDateTime(), {}});
    QCOMPARE(dialog.draft().quantity, 5000000000LL);
    QVERIFY(qobject_cast<QLineEdit *>(dialog.findChild<QWidget *>(QStringLiteral("quantity"))));
}

void DocumentDialogsTest::inboundCreatesAndUpdatesDraft()
{
    InboundDialog create(*master, *inbound);
    QVERIFY(create.initialize().ok());
    create.setDraft({QStringLiteral("B-CREATE"), productTypeId, 23, destinationId,
                     QDateTime::fromString(QStringLiteral("2026-09-10T08:30:00"), Qt::ISODate),
                     QStringLiteral("created")});
    QVERIFY(create.save());
    const auto listed = inbound->list();
    QVERIFY(listed.ok());
    QCOMPARE(listed.value().size(), 2);
    const auto created = std::find_if(listed.value().cbegin(), listed.value().cend(),
        [](const InboundBatch &item) { return item.batchNo == QStringLiteral("B-CREATE"); });
    QVERIFY(created != listed.value().cend());
    QCOMPARE(created->quantity, qint64(23));
    QCOMPARE(created->initialLocationId, destinationId);

    InboundDialog update(*master, *inbound);
    QVERIFY(update.initialize(created->id).ok());
    update.setDraft({QStringLiteral("B-UPDATED"), productTypeId, 29, sourceId,
                     QDateTime::fromString(QStringLiteral("2026-09-11T09:45:00"), Qt::ISODate),
                     QStringLiteral("updated")});
    QVERIFY(update.save());
    const auto loaded = inbound->byId(created->id);
    QVERIFY(loaded.ok());
    QCOMPARE(loaded.value().batchNo, QStringLiteral("B-UPDATED"));
    QCOMPARE(loaded.value().quantity, qint64(29));
    QCOMPARE(loaded.value().initialLocationId, sourceId);
    QCOMPARE(loaded.value().note, QStringLiteral("updated"));
}

void DocumentDialogsTest::inboundFocusesInvalidFieldAndLoadsDisabledReference()
{
    const auto created = inbound->createDraft({QStringLiteral("B-DRAFT"), productTypeId, 5,
        sourceId, QDateTime::currentDateTime(), {}}); QVERIFY(created.ok());
    QVERIFY(master->setProductTypeEnabled(productTypeId, false).ok());
    InboundDialog edit(*master, *inbound); QVERIFY(edit.initialize(created.value()).ok());
    auto *productTypes = edit.findChild<QComboBox *>(QStringLiteral("productTypeId"));
    QCOMPARE(productTypes->currentData().toLongLong(), productTypeId);
    edit.setDraft({{}, productTypeId, 5, sourceId, QDateTime::currentDateTime(), {}});
    edit.show();
    QCoreApplication::processEvents();
    QVERIFY(!edit.save());
    auto *batchNo = edit.findChild<QLineEdit *>(QStringLiteral("batchNo"));
    QVERIFY(batchNo->hasFocus());
    QVERIFY(!edit.findChild<QLabel *>(QStringLiteral("errorLabel"))->text().isEmpty());
}

void DocumentDialogsTest::newDialogsFilterDisabledLocations()
{
    const auto disabledInventory = addLocation(QStringLiteral("停用仓"), true);
    QVERIFY(master->setLocationEnabled(disabledInventory, false).ok());

    InboundDialog inboundDialog(*master, *inbound);
    QVERIFY(inboundDialog.initialize().ok());
    auto *inboundLocations = inboundDialog.findChild<QComboBox *>(QStringLiteral("initialLocationId"));
    QVERIFY(inboundLocations);
    QCOMPARE(inboundLocations->findData(disabledInventory), -1);

    LogisticsDialog logisticsDialog(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(logisticsDialog.initialize().ok());
    auto *sources = logisticsDialog.findChild<QComboBox *>(QStringLiteral("sourceLocationId"));
    auto *destinations = logisticsDialog.findChild<QComboBox *>(QStringLiteral("destinationLocationId"));
    QVERIFY(sources);
    QVERIFY(destinations);
    QCOMPARE(sources->findData(disabledInventory), -1);
    QCOMPARE(destinations->findData(disabledInventory), -1);
    QVERIFY(logisticsDialog.addLine().ok());
    QVERIFY(logisticsDialog.setLineBatch(0, batchId).ok());
}

void DocumentDialogsTest::logisticsFiltersBatchesAndDestinations()
{
    const auto emptyLocation = addLocation(QStringLiteral("空仓"), true);
    LogisticsDialog transfer(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(transfer.initialize().ok());
    QVERIFY(transfer.setSourceLocation(sourceId).ok());
    QCOMPARE(transfer.visibleBatchIds(), QVector<EntityId>{batchId});
    QVERIFY(transfer.setSourceLocation(emptyLocation).ok());
    QVERIFY(transfer.visibleBatchIds().isEmpty());
    QVERIFY(transfer.setSourceLocation(sourceId).ok());
    QVERIFY(transfer.addLine().ok());
    QVERIFY(transfer.setLineBatch(0, batchId).ok());
    auto *lines = transfer.findChild<QTableWidget *>(QStringLiteral("lines"));
    auto *batchChoices = qobject_cast<QComboBox *>(lines->cellWidget(0, 0));
    QVERIFY(batchChoices);
    QCOMPARE(batchChoices->count(), 1);
    QCOMPARE(batchChoices->itemData(0).toLongLong(), batchId);
    auto *destination = transfer.findChild<QComboBox *>(QStringLiteral("destinationLocationId"));
    QCOMPARE(destination->findData(sourceId), -1);
    QVERIFY(destination->findData(destinationId) >= 0);
    QCOMPARE(destination->findData(externalId), -1);

    LogisticsDialog outbound(*master, *queries, *logistics, DocumentType::Outbound);
    QVERIFY(outbound.initialize().ok());
    auto *outboundDestination = outbound.findChild<QComboBox *>(QStringLiteral("destinationLocationId"));
    QVERIFY(outboundDestination->findData(externalId) >= 0);
    QCOMPARE(outboundDestination->findData(destinationId), -1);
}

void DocumentDialogsTest::transferSourceChangeDropsMatchingDestinationHistory()
{
    const auto otherSource = addLocation(QStringLiteral("备用仓"), true);
    LogisticsDialog dialog(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(dialog.initialize().ok());
    auto *destination = dialog.findChild<QComboBox *>(QStringLiteral("destinationLocationId"));
    QVERIFY(destination);
    const int otherDestinationIndex = destination->findData(otherSource);
    QVERIFY(otherDestinationIndex >= 0);
    destination->setCurrentIndex(otherDestinationIndex);

    auto *source = dialog.findChild<QComboBox *>(QStringLiteral("sourceLocationId"));
    QVERIFY(source);
    const int otherSourceIndex = source->findData(otherSource);
    QVERIFY(otherSourceIndex >= 0);
    source->setCurrentIndex(otherSourceIndex);

    QCOMPARE(destination->findData(otherSource), -1);
}

void DocumentDialogsTest::transferSourceChangeDropsUnavailableLineBatch()
{
    const auto otherSource = addLocation(QStringLiteral("备用仓"), true);
    LogisticsDialog dialog(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(dialog.initialize().ok());
    QVERIFY(dialog.setSourceLocation(sourceId).ok());
    QVERIFY(dialog.addLine().ok());
    QVERIFY(dialog.setLineBatch(0, batchId).ok());

    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("lines"));
    auto *batch = qobject_cast<QComboBox *>(table->cellWidget(0, 0));
    QVERIFY(batch);
    QCOMPARE(batch->findData(batchId), 0);

    auto *source = dialog.findChild<QComboBox *>(QStringLiteral("sourceLocationId"));
    QVERIFY(source);
    const int otherSourceIndex = source->findData(otherSource);
    QVERIFY(otherSourceIndex >= 0);
    source->setCurrentIndex(otherSourceIndex);

    QCOMPARE(batch->findData(batchId), -1);
    QCOMPARE(batch->count(), 0);
}

void DocumentDialogsTest::newLineLoadsDefaultBatch()
{
    LogisticsDialog dialog(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(dialog.initialize().ok());
    QVERIFY(dialog.setSourceLocation(sourceId).ok());
    QVERIFY(dialog.addLine().ok());
    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("lines"));
    auto *batch = qobject_cast<QComboBox *>(table->cellWidget(0, 0));
    QCOMPARE(batch->currentData().toLongLong(), batchId);
}

void DocumentDialogsTest::logisticsDialogPreselectsPreferredBatch()
{
    const auto second = addConfirmedBatch(QStringLiteral("B-PREFERRED"), 100);
    QVERIFY(second > 0);
    LogisticsDialog dialog(*master, *queries, *logistics, DocumentType::Outbound);
    QVERIFY(dialog.initialize(0, false, second).ok());
    auto *lines = dialog.findChild<QTableWidget *>(QStringLiteral("lines"));
    QVERIFY(lines);
    auto *batch = qobject_cast<QComboBox *>(lines->cellWidget(0, 0));
    QVERIFY(batch);
    QCOMPARE(batch->currentData().toLongLong(), second);
}

void DocumentDialogsTest::sourceQueryFailureClearsStaleLineChoices()
{
    LogisticsDialog dialog(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(dialog.initialize().ok());
    QVERIFY(dialog.setSourceLocation(sourceId).ok());
    QVERIFY(dialog.addLine().ok());
    QVERIFY(db->execute(QStringLiteral("DROP TABLE inventory_entries")));
    const auto result = dialog.setSourceLocation(sourceId);
    QVERIFY(!result.ok());
    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("lines"));
    QCOMPARE(qobject_cast<QComboBox *>(table->cellWidget(0, 0))->count(), 0);
}

void DocumentDialogsTest::deletingEarlierLineKeepsCascadeBoundToCurrentRow()
{
    LogisticsDialog dialog(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(dialog.initialize().ok());
    QVERIFY(dialog.setSourceLocation(sourceId).ok());
    QVERIFY(dialog.addLine().ok());
    QVERIFY(dialog.addLine().ok());
    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("lines"));
    table->removeRow(0);
    QVERIFY(dialog.setLineBatch(0, batchId).ok());
}

void DocumentDialogsTest::pageRequestSignalsEmitOnce()
{
    InboundPage inboundPage(*master, *inbound);
    QSignalSpy inboundEdit(&inboundPage, &InboundPage::editRequested);
    auto *inboundTable = inboundPage.findChild<QTableWidget *>(QStringLiteral("documents"));
    inboundTable->selectRow(0);
    QTimer::singleShot(0, [] { if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject(); });
    QVERIFY(QMetaObject::invokeMethod(&inboundPage, "viewSelected"));
    QCOMPARE(inboundEdit.count(), 0);

    LogisticsPage logisticsPage(*master, *queries, *logistics, DocumentType::Transfer);
    QSignalSpy createSpy(&logisticsPage, &LogisticsPage::createRequested);
    QTimer::singleShot(0, [] { if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject(); });
    logisticsPage.findChild<QPushButton *>(QStringLiteral("createButton"))->click();
    QCOMPARE(createSpy.count(), 1);
}

void DocumentDialogsTest::logisticsCreatesAndUpdatesDraft()
{
    LogisticsDraft value{QStringLiteral("T-CREATE"), DocumentType::Transfer, sourceId,
        destinationId, QDateTime::currentDateTime(), QStringLiteral("create"),
        {{batchId, 9}}};
    LogisticsDialog create(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(create.initialize().ok());
    QVERIFY(create.setDraft(value).ok());
    QVERIFY(create.save());
    const auto listed = logistics->list(DocumentType::Transfer);
    QVERIFY(listed.ok()); QCOMPARE(listed.value().size(), 1);
    QCOMPARE(listed.value().first().documentNo, QStringLiteral("T-CREATE"));

    LogisticsDialog update(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(update.initialize(listed.value().first().id).ok());
    value.documentNo = QStringLiteral("T-UPDATED");
    value.lines.first().quantity = 11;
    QVERIFY(update.setDraft(value).ok());
    QVERIFY(update.save());
    const auto loaded = logistics->byId(listed.value().first().id);
    QVERIFY(loaded.ok()); QCOMPARE(loaded.value().documentNo, QStringLiteral("T-UPDATED"));
    QCOMPARE(loaded.value().lines.first().quantity, 11);
}

void DocumentDialogsTest::logisticsRejectsEditingNonDrafts()
{
    LogisticsDraft value{QStringLiteral("T-STATE"), DocumentType::Transfer, sourceId,
        destinationId, QDateTime::currentDateTime(), {}, {{batchId, 2}}};
    const auto confirmed = logistics->createDraft(value); QVERIFY(confirmed.ok());
    QVERIFY(logistics->confirm(confirmed.value()).ok());
    LogisticsDialog confirmedEdit(*master, *queries, *logistics, DocumentType::Transfer);
    const auto confirmedResult = confirmedEdit.initialize(confirmed.value());
    QVERIFY(!confirmedResult.ok()); QCOMPARE(confirmedResult.error().code, QStringLiteral("invalid_status"));
    QVERIFY(logistics->cancel(confirmed.value()).ok());
    LogisticsDialog cancelledEdit(*master, *queries, *logistics, DocumentType::Transfer);
    const auto cancelledResult = cancelledEdit.initialize(confirmed.value());
    QVERIFY(!cancelledResult.ok()); QCOMPARE(cancelledResult.error().code, QStringLiteral("invalid_status"));
}

void DocumentDialogsTest::logisticsLoadsDisabledHistoricalLocations()
{
    LogisticsDraft value{QStringLiteral("T-HISTORY-LOCATION"), DocumentType::Transfer, sourceId,
        destinationId, QDateTime::currentDateTime(), {}, {{batchId, 2}}};
    const auto created = logistics->createDraft(value); QVERIFY(created.ok());
    QVERIFY(master->setLocationEnabled(sourceId, false).ok());
    QVERIFY(master->setLocationEnabled(destinationId, false).ok());
    LogisticsDialog dialog(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(dialog.initialize(created.value()).ok());
    auto *source = dialog.findChild<QComboBox *>(QStringLiteral("sourceLocationId"));
    auto *destination = dialog.findChild<QComboBox *>(QStringLiteral("destinationLocationId"));
    QCOMPARE(source->currentData().toLongLong(), sourceId);
    QCOMPARE(destination->currentData().toLongLong(), destinationId);
    QVERIFY(source->currentText().contains(QStringLiteral("已停用")));
    QVERIFY(destination->currentText().contains(QStringLiteral("已停用")));
}

void DocumentDialogsTest::logisticsLoadsZeroBalanceHistoricalLineReadOnly()
{
    LogisticsDraft value{QStringLiteral("O-HISTORY-BATCH"), DocumentType::Outbound, sourceId,
        externalId, QDateTime::currentDateTime(), {}, {{batchId, 5000000000LL}}};
    const auto created = logistics->createDraft(value); QVERIFY(created.ok());
    QVERIFY(logistics->confirm(created.value()).ok());
    LogisticsDialog view(*master, *queries, *logistics, DocumentType::Outbound);
    QVERIFY(view.initialize(created.value(), true).ok());
    auto *table = view.findChild<QTableWidget *>(QStringLiteral("lines"));
    auto *batch = qobject_cast<QComboBox *>(table->cellWidget(0, 0));
    auto *quantity = qobject_cast<QLineEdit *>(table->cellWidget(0, 1));
    QCOMPARE(batch->currentData().toLongLong(), batchId);
    QCOMPARE(quantity->text(), QStringLiteral("5000000000"));
    QVERIFY(!batch->isEnabled()); QVERIFY(!quantity->isEnabled());
}

void DocumentDialogsTest::logisticsFocusesExactLineField()
{
    LogisticsDialog dialog(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(dialog.initialize().ok());
    const EntityId secondBatch = addConfirmedBatch(QStringLiteral("B-2"), 100);
    QVERIFY(secondBatch);
    LogisticsDraft value{QStringLiteral("T-INVALID"), DocumentType::Transfer, sourceId,
        destinationId, QDateTime::currentDateTime(), {},
        {{batchId, 1}, {secondBatch, 0}}};
    QVERIFY(dialog.setDraft(value).ok());
    dialog.show();
    QCoreApplication::processEvents();
    QVERIFY(!dialog.save());
    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("lines"));
    auto *quantity = qobject_cast<QLineEdit *>(table->cellWidget(1, 1));
    QVERIFY(quantity->hasFocus());
    QVERIFY(!dialog.findChild<QLabel *>(QStringLiteral("errorLabel"))->text().isEmpty());
}

void DocumentDialogsTest::logisticsQueryErrorsPropagate()
{
    LogisticsDialog dialog(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(dialog.initialize().ok());
    QVERIFY(db->execute(QStringLiteral("DROP TABLE inventory_entries")));
    const auto result = dialog.setSourceLocation(sourceId);
    QVERIFY(!result.ok());
    QVERIFY(!result.error().code.isEmpty());
    QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("errorLabel"))->text(), result.error().message);
}

void DocumentDialogsTest::logisticsDestinationQueryErrorsPropagate()
{
    LogisticsDialog dialog(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(dialog.initialize().ok());
    QVERIFY(db->execute(QStringLiteral("PRAGMA foreign_keys=OFF")));
    QVERIFY(db->execute(QStringLiteral("DROP TABLE locations")));
    const auto result = dialog.setSourceLocation(sourceId);
    QVERIFY(!result.ok());
    QCOMPARE(result.error().code, QStringLiteral("database"));
    QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("errorLabel"))->text(), result.error().message);
}

void DocumentDialogsTest::logisticsPreservesMultipleLinesAndReadOnlyState()
{
    const EntityId secondBatch = addConfirmedBatch(QStringLiteral("B-2"), 100);
    QVERIFY(secondBatch);
    LogisticsDraft value{QStringLiteral("T-1"), DocumentType::Transfer, sourceId,
        destinationId, QDateTime::currentDateTime(), {},
        {{batchId, 3000000000LL}, {secondBatch, 7}}};
    LogisticsDialog dialog(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(dialog.initialize().ok());
    QVERIFY(dialog.setDraft(value).ok());
    QCOMPARE(dialog.draft().lines.size(), 2);
    QCOMPARE(dialog.draft().lines.at(0).quantity, 3000000000LL);
    const auto created = logistics->createDraft(value); QVERIFY(created.ok());
    QVERIFY(logistics->confirm(created.value()).ok());
    LogisticsDialog view(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(view.initialize(created.value(), true).ok());
    QVERIFY(view.isReadOnly());
    QVERIFY(!view.findChild<QLineEdit *>(QStringLiteral("documentNo"))->isEnabled());
    QVERIFY(!view.findChild<QComboBox *>(QStringLiteral("sourceLocationId"))->isEnabled());
    QVERIFY(!view.findChild<QComboBox *>(QStringLiteral("destinationLocationId"))->isEnabled());
    QVERIFY(!view.findChild<QPushButton *>(QStringLiteral("addLineButton"))->isEnabled());
}

void DocumentDialogsTest::shortageContainsBusinessDetailsAndExactField()
{
    LogisticsDraft value{QStringLiteral("O-TOO-MUCH"), DocumentType::Outbound, sourceId,
        externalId, QDateTime::currentDateTime(), {},
        {{batchId, 6000000000LL}}};
    const auto created = logistics->createDraft(value); QVERIFY(created.ok());
    const auto result = logistics->confirm(created.value());
    QVERIFY(!result.ok());
    QCOMPARE(result.error().code, QStringLiteral("insufficient_stock"));
    QVERIFY(result.error().message.contains(QStringLiteral("B-1")));
    QVERIFY(result.error().message.contains(QStringLiteral("公司仓")));
    QVERIFY(result.error().message.contains(QStringLiteral("5000000000")));
    QVERIFY(result.error().message.contains(QStringLiteral("6000000000")));
    QCOMPARE(result.error().field, QStringLiteral("lines[0].quantity"));
}

void DocumentDialogsTest::inboundPageExposesDialogInitializeFailures_data()
{
    QTest::addColumn<QString>("action");
    QTest::newRow("create") << QStringLiteral("create");
    QTest::newRow("edit") << QStringLiteral("edit");
    QTest::newRow("view") << QStringLiteral("view");
}

void DocumentDialogsTest::inboundPageExposesDialogInitializeFailures()
{
    QFETCH(QString, action);
    EntityId selected = 0;
    if (action == QStringLiteral("edit")) {
        const auto draft = inbound->createDraft({QStringLiteral("B-PAGE-EDIT"), productTypeId, 1,
            sourceId, QDateTime::currentDateTime(), {}});
        QVERIFY(draft.ok());
        selected = draft.value();
    } else if (action == QStringLiteral("view")) {
        selected = batchId;
    }

    InboundPage page(*master, *inbound);
    auto *table = page.findChild<QTableWidget *>(QStringLiteral("documents"));
    auto *error = page.findChild<QLabel *>(QStringLiteral("errorLabel"));
    QVERIFY(table);
    QVERIFY(error);
    if (selected) {
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->item(row, 0)->text().toLongLong() == selected) {
                table->selectRow(row);
                break;
            }
        }
        QCOMPARE(table->item(table->currentRow(), 0)->text().toLongLong(), selected);
        QVERIFY(db->execute(QStringLiteral("ALTER TABLE inbound_batches RENAME TO unavailable_inbound_batches")));
        QVERIFY(QMetaObject::invokeMethod(&page, action == QStringLiteral("edit") ? "editSelected" : "viewSelected"));
    } else {
        QVERIFY(db->execute(QStringLiteral("ALTER TABLE product_categories RENAME TO unavailable_categories")));
        auto *create = page.findChild<QPushButton *>(QStringLiteral("createButton"));
        QVERIFY(create);
        create->click();
    }
    QVERIFY(!page.lastError().code.isEmpty());
    QVERIFY(!error->text().isEmpty());
    QCOMPARE(error->text(), page.lastError().message);
}

void DocumentDialogsTest::logisticsPageExposesDialogInitializeFailures_data()
{
    QTest::addColumn<QString>("action");
    QTest::newRow("create") << QStringLiteral("create");
    QTest::newRow("edit") << QStringLiteral("edit");
    QTest::newRow("view") << QStringLiteral("view");
}

void DocumentDialogsTest::logisticsPageExposesDialogInitializeFailures()
{
    QFETCH(QString, action);
    EntityId selected = 0;
    if (action != QStringLiteral("create")) {
        const auto document = logistics->createDraft({QStringLiteral("T-PAGE-INIT"),
            DocumentType::Transfer, sourceId, destinationId, QDateTime::currentDateTime(), {},
            {{batchId, 1}}});
        QVERIFY(document.ok());
        selected = document.value();
        if (action == QStringLiteral("view"))
            QVERIFY(logistics->confirm(selected).ok());
    }

    LogisticsPage page(*master, *queries, *logistics, DocumentType::Transfer);
    auto *table = page.findChild<QTableWidget *>(QStringLiteral("documents"));
    auto *error = page.findChild<QLabel *>(QStringLiteral("errorLabel"));
    QVERIFY(table);
    QVERIFY(error);
    if (selected) {
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->item(row, 0)->text().toLongLong() == selected) {
                table->selectRow(row);
                break;
            }
        }
        QCOMPARE(table->item(table->currentRow(), 0)->text().toLongLong(), selected);
        QVERIFY(db->execute(QStringLiteral("ALTER TABLE logistics_documents RENAME TO unavailable_logistics_documents")));
        QVERIFY(QMetaObject::invokeMethod(&page, action == QStringLiteral("edit") ? "editSelected" : "viewSelected"));
    } else {
        QVERIFY(db->execute(QStringLiteral("ALTER TABLE locations RENAME TO unavailable_locations")));
        auto *create = page.findChild<QPushButton *>(QStringLiteral("createButton"));
        QVERIFY(create);
        create->click();
    }
    QVERIFY(!page.lastError().code.isEmpty());
    QVERIFY(!error->text().isEmpty());
    QCOMPARE(error->text(), page.lastError().message);
}

void DocumentDialogsTest::pagesExposeFailuresAndClearErrorsOnSuccess()
{
    const auto consumed = logistics->createDraft({QStringLiteral("O-CONSUME"), DocumentType::Outbound,
        sourceId, externalId, QDateTime::currentDateTime(), {}, {{batchId, 1}}});
    QVERIFY(consumed.ok());
    QVERIFY(logistics->confirm(consumed.value()).ok());

    InboundPage inboundPage(*master, *inbound);
    QSignalSpy inboundChanged(&inboundPage, &InboundPage::dataChanged);
    auto *inboundError = inboundPage.findChild<QLabel *>(QStringLiteral("errorLabel"));
    QVERIFY(inboundError); inboundPage.setConfirmationCallback([](const QString &) { return true; });
    auto *inboundTable = inboundPage.findChild<QTableWidget *>(QStringLiteral("documents"));
    inboundTable->selectRow(0);
    QVERIFY(QMetaObject::invokeMethod(&inboundPage, "cancelSelected"));
    QCOMPARE(inboundPage.lastError().code, QStringLiteral("stock_dependency"));
    QCOMPARE(inboundError->text(), inboundPage.lastError().message);
    QCOMPARE(inboundChanged.count(), 0);

    const auto inboundDraft = inbound->createDraft({QStringLiteral("B-PAGE-SUCCESS"), productTypeId, 1,
        sourceId, QDateTime::currentDateTime(), {}}); QVERIFY(inboundDraft.ok());
    QVERIFY(inboundPage.refresh().ok());
    inboundChanged.clear();
    inboundTable->selectRow(0);
    QVERIFY(QMetaObject::invokeMethod(&inboundPage, "confirmSelected"));
    QVERIFY(inboundPage.lastError().code.isEmpty());
    QVERIFY(inboundError->text().isEmpty());
    QCOMPARE(inboundChanged.count(), 1);

    LogisticsDraft tooMuch{QStringLiteral("O-PAGE-FAIL"), DocumentType::Outbound, sourceId,
        externalId, QDateTime::currentDateTime(), {}, {{batchId, 6000000000LL}}};
    const auto created = logistics->createDraft(tooMuch); QVERIFY(created.ok());
    LogisticsPage logisticsPage(*master, *queries, *logistics, DocumentType::Outbound);
    QSignalSpy logisticsChanged(&logisticsPage, &LogisticsPage::dataChanged);
    auto *logisticsError = logisticsPage.findChild<QLabel *>(QStringLiteral("errorLabel"));
    QVERIFY(logisticsError); logisticsPage.setConfirmationCallback([](const QString &) { return true; });
    auto *logisticsTable = logisticsPage.findChild<QTableWidget *>(QStringLiteral("documents"));
    logisticsTable->selectRow(0);
    QVERIFY(QMetaObject::invokeMethod(&logisticsPage, "confirmSelected"));
    QCOMPARE(logisticsPage.lastError().code, QStringLiteral("insufficient_stock"));
    QCOMPARE(logisticsError->text(), logisticsPage.lastError().message);
    QVERIFY(logisticsError->text().contains(QStringLiteral("可用")));
    QVERIFY(logisticsError->text().contains(QStringLiteral("请求")));
    QCOMPARE(logisticsChanged.count(), 0);
}

void DocumentDialogsTest::logisticsPageConfirmationAndDataChanged()
{
    LogisticsDraft value{QStringLiteral("T-PAGE"), DocumentType::Transfer, sourceId,
        destinationId, QDateTime::currentDateTime(), {}, {{batchId, 1}}};
    const auto created = logistics->createDraft(value); QVERIFY(created.ok());
    LogisticsPage page(*master, *queries, *logistics, DocumentType::Transfer);
    QSignalSpy changed(&page, &LogisticsPage::dataChanged);
    int callbacks = 0;
    page.setConfirmationCallback([&callbacks](const QString &) { ++callbacks; return true; });
    auto *table = page.findChild<QTableWidget *>(QStringLiteral("documents"));
    table->selectRow(0);
    QVERIFY(QMetaObject::invokeMethod(&page, "confirmSelected"));
    QCOMPARE(callbacks, 1); QCOMPARE(changed.count(), 1);
    QVERIFY(page.lastError().code.isEmpty());
}

void DocumentDialogsTest::pageConfirmationAndDataChanged()
{
    InboundPage page(*master, *inbound);
    QSignalSpy changed(&page, &InboundPage::dataChanged);
    page.setConfirmationCallback([](const QString &) { return false; });
    auto *table = page.findChild<QTableWidget *>(QStringLiteral("documents"));
    table->selectRow(0);
    QVERIFY(QMetaObject::invokeMethod(&page, "cancelSelected"));
    QCOMPARE(changed.count(), 0);
    page.setConfirmationCallback([](const QString &) { return true; });
    QVERIFY(QMetaObject::invokeMethod(&page, "cancelSelected"));
    QCOMPARE(changed.count(), 1);
}

void DocumentDialogsTest::outboundModelCascadeAndZeroStockReturn()
{
    const auto f25 = master->createProductModel(productTypeId, QStringLiteral("F25"), {});
    const auto p25 = master->createProductModel(productTypeId, QStringLiteral("P25"), {});
    const auto otherType = master->createProductType(categoryId, QStringLiteral("其他类型"), {});
    QVERIFY(f25.ok() && p25.ok() && otherType.ok());
    const auto otherModel = master->createProductModel(otherType.value().id, QStringLiteral("MQTT"), {});
    QVERIFY(otherModel.ok());
    const auto second = inbound->createDraft({QStringLiteral("B-OTHER"), otherType.value().id,
        10, sourceId, QDateTime::currentDateTime(), {}});
    QVERIFY(second.ok());
    QVERIFY(inbound->confirm(second.value()).ok());

    LogisticsDialog outbound(*master, *queries, *logistics, DocumentType::Outbound);
    QVERIFY(outbound.initialize().ok());
    QVERIFY(outbound.setSourceLocation(sourceId).ok());
    QVERIFY(outbound.addLine().ok());
    QVERIFY(outbound.addLine().ok());
    auto *lines = outbound.findChild<QTableWidget *>(QStringLiteral("lines"));
    lines->removeRow(0);
    QVERIFY(outbound.setLineBatch(0, batchId).ok());
    auto *models = qobject_cast<QComboBox *>(lines->cellWidget(0, 2));
    QVERIFY(models);
    QVERIFY(models->findData(f25.value().id) >= 0);
    QCOMPARE(models->findData(otherModel.value().id), -1);
    QVERIFY(outbound.setLineBatch(0, second.value()).ok());
    QCOMPARE(models->findData(f25.value().id), -1);
    QVERIFY(models->findData(otherModel.value().id) >= 0);

    LogisticsDraft draft{QStringLiteral("O-MODELS"), DocumentType::Outbound, sourceId, externalId,
        QDateTime::currentDateTime(), {}, {{batchId, 3000000000LL, f25.value().id},
                                         {batchId, 2000000000LL, p25.value().id}}};
    QVERIFY(outbound.setDraft(draft).ok());
    QCOMPARE(outbound.draft().lines.front().productModelId, f25.value().id);
    QVERIFY(outbound.save());
    const auto sent = logistics->list(DocumentType::Outbound);
    QVERIFY(sent.ok());
    QCOMPARE(sent.value().size(), 1);
    QVERIFY(logistics->confirm(sent.value().front().id).ok());
    QCOMPARE(queries->batchRemaining(batchId).value(), qint64(0));
    QVERIFY(master->setProductModelEnabled(f25.value().id, false).ok());

    LogisticsDialog returned(*master, *queries, *logistics, DocumentType::Return);
    QVERIFY(returned.initialize().ok());
    QVERIFY(returned.setSourceLocation(externalId).ok());
    QCOMPARE(returned.visibleBatchIds(), QVector<EntityId>{batchId});
    auto *destinations = returned.findChild<QComboBox *>(QStringLiteral("destinationLocationId"));
    QVERIFY(destinations->findData(sourceId) >= 0);
    QCOMPARE(destinations->findData(externalId), -1);
    QVERIFY(returned.setDraft({QStringLiteral("R-MODELS"), DocumentType::Return, externalId, sourceId,
        QDateTime::currentDateTime(), {}, {{batchId, 50, f25.value().id}}}).ok());
    QVERIFY(returned.save());
    const auto saved = logistics->list(DocumentType::Return);
    QVERIFY(saved.ok());
    QCOMPARE(saved.value().size(), 1);
    QVERIFY(logistics->confirm(saved.value().front().id).ok());
    QCOMPARE(queries->batchRemaining(batchId).value(), qint64(50));
    LogisticsDialog view(*master, *queries, *logistics, DocumentType::Return);
    QVERIFY(view.initialize(saved.value().front().id, true).ok());
    auto *viewLines = view.findChild<QTableWidget *>(QStringLiteral("lines"));
    auto *viewModel = qobject_cast<QComboBox *>(viewLines->cellWidget(0, 2));
    QVERIFY(viewModel);
    QCOMPARE(viewModel->currentData().toLongLong(), f25.value().id);
    QVERIFY(!viewModel->isEnabled());
    const auto historyDraft = logistics->createDraft({QStringLiteral("O-HISTORY-MODEL"),
        DocumentType::Outbound, sourceId, externalId, QDateTime::currentDateTime(), {},
        {{batchId, 1, f25.value().id}}});
    QVERIFY(historyDraft.ok());
    LogisticsDialog edit(*master, *queries, *logistics, DocumentType::Outbound);
    QVERIFY(edit.initialize(historyDraft.value()).ok());
    auto *editLines = edit.findChild<QTableWidget *>(QStringLiteral("lines"));
    auto *editModel = qobject_cast<QComboBox *>(editLines->cellWidget(0, 2));
    QCOMPARE(editModel->currentData().toLongLong(), f25.value().id);
    QVERIFY(editModel->currentText().contains(QStringLiteral("F25")));
    QVERIFY(master->updateProductModel(f25.value().id, productTypeId, QStringLiteral("F25-NEW"), {}).ok());
    QVERIFY(master->setProductModelEnabled(f25.value().id, true).ok());
    LogisticsDialog history(*master, *queries, *logistics, DocumentType::Outbound);
    QVERIFY(history.initialize(sent.value().front().id, true).ok());
    auto *historyLines = history.findChild<QTableWidget *>(QStringLiteral("lines"));
    QCOMPARE(qobject_cast<QComboBox *>(historyLines->cellWidget(0, 2))->currentText(), QStringLiteral("F25"));
}

QTEST_MAIN(DocumentDialogsTest)
#include "tst_document_dialogs.moc"
