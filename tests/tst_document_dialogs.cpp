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
    void inboundCreatesAndUpdatesRecord();
    void inboundFocusesInvalidFieldAndLoadsDisabledReference();
    void newDialogsFilterDisabledLocations();
    void logisticsFiltersBatchesAndDestinations();
    void transferSourceChangeKeepsFactoryDestination();
    void transferSourceChangePreservesLine();
    void sourceQueryFailurePreservesLineChoices();
    void deletingEarlierLineKeepsCascadeBoundToCurrentRow();
    void newLineLoadsDefaultBatch();
    void logisticsDialogPreselectsPreferredBatch();
    void pageRequestSignalsEmitOnce();
    void logisticsCreatesAndUpdatesRecord();
    void logisticsEditsConfirmedAndCancelledRecords();
    void logisticsLoadsDisabledHistoricalLocations();
    void logisticsLoadsZeroBalanceHistoricalLineReadOnly();
    void logisticsFocusesExactLineField();
    void logisticsQueryErrorsPropagate();
    void logisticsDestinationQueryErrorsPropagate();
    void logisticsPreservesMultipleLinesAndReadOnlyState();
    void shortageSavesNegativeBalance();
    void inboundPageExposesDialogInitializeFailures_data();
    void inboundPageExposesDialogInitializeFailures();
    void logisticsPageExposesDialogInitializeFailures_data();
    void logisticsPageExposesDialogInitializeFailures();
    void pagesExposeFailuresAndClearErrorsOnSuccess();
    void logisticsPageCancellationAndDataChanged();
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
    EntityId personId = 0;
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
    const auto category = master->createCategory(QStringLiteral("设备"));
    QVERIFY(category.ok());
    categoryId = category.value().id;
    const auto productType = master->createProductType(categoryId, QStringLiteral("GW"), {});
    QVERIFY(productType.ok());
    productTypeId = productType.value().id;
    sourceId = addLocation(QStringLiteral("公司仓"), true);
    destinationId = addLocation(QStringLiteral("工厂仓"), true, LocationType::Factory);
    externalId = addLocation(QStringLiteral("Amazon"), false, LocationType::Other);
    const auto person = master->createPersonnel(QStringLiteral("经办人"));
    QVERIFY(person.ok());
    personId = person.value().id;
    batchId = addConfirmedBatch(QStringLiteral("B-1"), 5000000000LL);
    QVERIFY(batchId > 0);
}

EntityId DocumentDialogsTest::addLocation(const QString &name, bool inventory, LocationType type)
{
    const auto result = master->createLocation(name, type, inventory);
    return result.ok() ? result.value().id : 0;
}

EntityId DocumentDialogsTest::addConfirmedBatch(const QString &batchNo, qint64 quantity)
{
    const auto created = inbound->create({batchNo, productTypeId, quantity, sourceId,
        QDateTime::currentDateTime(), {}, QStringLiteral("V40"), personId});
    return created.ok() ? created.value() : 0;
}

void DocumentDialogsTest::inboundFiltersDisabledAndAcceptsQint64()
{
    const auto disabled = master->createProductType(categoryId, QStringLiteral("OFF"), {});
    QVERIFY(disabled.ok());
    QVERIFY(master->setProductTypeEnabled(disabled.value().id, false).ok());
    InboundDialog dialog(*master, *inbound);
    QVERIFY(dialog.initialize().ok());
    auto *productTypes = dialog.findChild<QComboBox *>(QStringLiteral("productTypeId"));
    QVERIFY(productTypes);
    QCOMPARE(productTypes->findData(disabled.value().id), -1);
    dialog.setInput({QStringLiteral("B-LARGE"), productTypeId, 5000000000LL,
        sourceId, QDateTime::currentDateTime(), {}, QStringLiteral("V40"), personId});
    QCOMPARE(dialog.input().quantity, 5000000000LL);
    QVERIFY(qobject_cast<QLineEdit *>(dialog.findChild<QWidget *>(QStringLiteral("quantity"))));
}

void DocumentDialogsTest::inboundCreatesAndUpdatesRecord()
{
    const EntityId otherCompany = addLocation(QStringLiteral("备用公司仓"), true);
    InboundDialog create(*master, *inbound);
    QVERIFY(create.initialize().ok());
    create.setInput({QStringLiteral("B-CREATE"), productTypeId, 23, otherCompany,
        QDateTime::fromString(QStringLiteral("2026-09-10T08:30:00"), Qt::ISODate),
        QStringLiteral("created"), QStringLiteral("V40"), personId});
    QVERIFY(create.save());
    const auto listed = inbound->list();
    QVERIFY(listed.ok());
    QCOMPARE(listed.value().size(), 2);
    const auto created = std::find_if(listed.value().cbegin(), listed.value().cend(),
        [](const InboundBatch &item) { return item.batchNo == QStringLiteral("B-CREATE"); });
    QVERIFY(created != listed.value().cend());
    QCOMPARE(created->quantity, qint64(23));
    QCOMPARE(created->initialLocationId, otherCompany);
    QCOMPARE(created->status, DocumentStatus::Confirmed);
    InboundDialog update(*master, *inbound);
    QVERIFY(update.initialize(created->id).ok());
    update.setInput({QStringLiteral("B-UPDATED"), productTypeId, 29, sourceId,
        QDateTime::fromString(QStringLiteral("2026-09-11T09:45:00"), Qt::ISODate),
        QStringLiteral("updated"), QStringLiteral("V40"), personId});
    QVERIFY(update.save());
    const auto loaded = inbound->byId(created->id);
    QVERIFY(loaded.ok());
    QCOMPARE(loaded.value().batchNo, QStringLiteral("B-UPDATED"));
    QCOMPARE(loaded.value().quantity, qint64(29));
    QCOMPARE(loaded.value().initialLocationId, sourceId);
    QCOMPARE(loaded.value().note, QStringLiteral("updated"));
    QCOMPARE(queries->balance(created->id, sourceId).value(), qint64(29));
    QCOMPARE(queries->balance(created->id, otherCompany).value(), qint64(0));
}

void DocumentDialogsTest::inboundFocusesInvalidFieldAndLoadsDisabledReference()
{
    const EntityId created = addConfirmedBatch(QStringLiteral("B-EDIT"), 5);
    QVERIFY(created > 0);
    QVERIFY(master->setProductTypeEnabled(productTypeId, false).ok());
    InboundDialog edit(*master, *inbound);
    QVERIFY(edit.initialize(created).ok());
    auto *productTypes = edit.findChild<QComboBox *>(QStringLiteral("productTypeId"));
    QCOMPARE(productTypes->currentData().toLongLong(), productTypeId);
    auto value = edit.input();
    value.quantity = 0;
    edit.setInput(value);
    edit.show();
    QCoreApplication::processEvents();
    QVERIFY(!edit.save());
    QVERIFY(edit.findChild<QLineEdit *>(QStringLiteral("quantity"))->hasFocus());
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
    QVERIFY(sources && destinations);
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
    QCOMPARE(transfer.visibleBatchIds(), QVector<EntityId>{batchId});
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

void DocumentDialogsTest::transferSourceChangeKeepsFactoryDestination()
{
    const auto otherSource = addLocation(QStringLiteral("备用仓"), true);
    LogisticsDialog dialog(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(dialog.initialize().ok());
    auto *destination = dialog.findChild<QComboBox *>(QStringLiteral("destinationLocationId"));
    QVERIFY(destination);
    destination->setCurrentIndex(destination->findData(destinationId));
    QVERIFY(dialog.setSourceLocation(otherSource).ok());
    QCOMPARE(destination->currentData().toLongLong(), destinationId);
    QCOMPARE(destination->findData(otherSource), -1);
}

void DocumentDialogsTest::transferSourceChangePreservesLine()
{
    const auto otherSource = addLocation(QStringLiteral("备用仓"), true);
    const auto model = master->createProductModel(productTypeId, QStringLiteral("F25"), {});
    QVERIFY(model.ok());
    LogisticsDialog dialog(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(dialog.initialize().ok());
    QVERIFY(dialog.setInput({QStringLiteral("T-SOURCE"), DocumentType::Transfer, sourceId,
        destinationId, QDateTime::currentDateTime(), {}, {{batchId, 17, model.value().id}}}).ok());
    auto *source = dialog.findChild<QComboBox *>(QStringLiteral("sourceLocationId"));
    source->setCurrentIndex(source->findData(otherSource));
    const auto value = dialog.input();
    QCOMPARE(value.lines.size(), 1);
    QCOMPARE(value.lines.front().batchId, batchId);
    QCOMPARE(value.lines.front().productModelId, model.value().id);
    QCOMPARE(value.lines.front().quantity, qint64(17));
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

void DocumentDialogsTest::sourceQueryFailurePreservesLineChoices()
{
    LogisticsDialog dialog(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(dialog.initialize().ok());
    QVERIFY(dialog.setSourceLocation(sourceId).ok());
    QVERIFY(dialog.addLine().ok());
    QVERIFY(db->execute(QStringLiteral("DROP TABLE inventory_entries")));
    const auto result = dialog.setSourceLocation(sourceId);
    QVERIFY(!result.ok());
    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("lines"));
    QCOMPARE(qobject_cast<QComboBox *>(table->cellWidget(0, 0))->currentData().toLongLong(), batchId);
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

void DocumentDialogsTest::logisticsCreatesAndUpdatesRecord()
{
    LogisticsInput value{QStringLiteral("T-CREATE"), DocumentType::Transfer, sourceId,
        destinationId, QDateTime::currentDateTime(), QStringLiteral("create"), {{batchId, 9}}};
    LogisticsDialog create(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(create.initialize().ok());
    QVERIFY(create.setInput(value).ok());
    QVERIFY(create.save());
    const auto listed = logistics->list(DocumentType::Transfer);
    QVERIFY(listed.ok());
    QCOMPARE(listed.value().size(), 1);
    QCOMPARE(listed.value().first().status, DocumentStatus::Confirmed);
    LogisticsDialog update(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(update.initialize(listed.value().first().id).ok());
    value.documentNo = QStringLiteral("T-UPDATED");
    value.lines.first().quantity = 11;
    QVERIFY(update.setInput(value).ok());
    QVERIFY(update.save());
    const auto loaded = logistics->byId(listed.value().first().id);
    QVERIFY(loaded.ok());
    QCOMPARE(loaded.value().documentNo, QStringLiteral("T-UPDATED"));
    QCOMPARE(loaded.value().lines.first().quantity, qint64(11));
    QCOMPARE(queries->balance(batchId, destinationId).value(), qint64(11));
}

void DocumentDialogsTest::logisticsEditsConfirmedAndCancelledRecords()
{
    LogisticsInput value{QStringLiteral("T-STATE"), DocumentType::Transfer, sourceId,
        destinationId, QDateTime::currentDateTime(), {}, {{batchId, 2}}};
    const auto confirmed = logistics->create(value);
    QVERIFY(confirmed.ok());
    LogisticsDialog confirmedEdit(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(confirmedEdit.initialize(confirmed.value()).ok());
    QCOMPARE(confirmedEdit.findChild<QPushButton *>(QStringLiteral("saveButton"))->text(), QStringLiteral("保存修改"));
    QVERIFY(logistics->cancel(confirmed.value()).ok());
    LogisticsDialog cancelledEdit(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(cancelledEdit.initialize(confirmed.value()).ok());
    QCOMPARE(cancelledEdit.findChild<QPushButton *>(QStringLiteral("saveButton"))->text(), QStringLiteral("保存并恢复生效"));
    QVERIFY(cancelledEdit.save());
    QCOMPARE(logistics->byId(confirmed.value()).value().status, DocumentStatus::Confirmed);
}

void DocumentDialogsTest::logisticsLoadsDisabledHistoricalLocations()
{
    LogisticsInput value{QStringLiteral("T-HISTORY-LOCATION"), DocumentType::Transfer, sourceId,
        destinationId, QDateTime::currentDateTime(), {}, {{batchId, 2}}};
    const auto created = logistics->create(value);
    QVERIFY(created.ok());
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
    QVERIFY(logistics->create({QStringLiteral("T-HISTORY-BATCH"), DocumentType::Transfer, sourceId,
        destinationId, QDateTime::currentDateTime(), {}, {{batchId, 5000000000LL}}}).ok());
    const auto created = logistics->create({QStringLiteral("O-HISTORY-BATCH"), DocumentType::Outbound,
        destinationId, externalId, QDateTime::currentDateTime(), {}, {{batchId, 5000000000LL}}, personId});
    QVERIFY(created.ok());
    LogisticsDialog view(*master, *queries, *logistics, DocumentType::Outbound);
    QVERIFY(view.initialize(created.value(), true).ok());
    auto *table = view.findChild<QTableWidget *>(QStringLiteral("lines"));
    auto *batch = qobject_cast<QComboBox *>(table->cellWidget(0, 0));
    auto *quantity = qobject_cast<QLineEdit *>(table->cellWidget(0, 1));
    QCOMPARE(batch->currentData().toLongLong(), batchId);
    QCOMPARE(quantity->text(), QStringLiteral("5000000000"));
    QVERIFY(!batch->isEnabled());
    QVERIFY(quantity->isReadOnly());
}

void DocumentDialogsTest::logisticsFocusesExactLineField()
{
    LogisticsDialog dialog(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(dialog.initialize().ok());
    const EntityId secondBatch = addConfirmedBatch(QStringLiteral("B-2"), 100);
    QVERIFY(secondBatch);
    QVERIFY(dialog.setInput({QStringLiteral("T-INVALID"), DocumentType::Transfer, sourceId,
        destinationId, QDateTime::currentDateTime(), {}, {{batchId, 1}, {secondBatch, 0}}}).ok());
    dialog.show();
    QCoreApplication::processEvents();
    QVERIFY(!dialog.save());
    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("lines"));
    QVERIFY(qobject_cast<QLineEdit *>(table->cellWidget(1, 1))->hasFocus());
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
    LogisticsInput value{QStringLiteral("T-1"), DocumentType::Transfer, sourceId,
        destinationId, QDateTime::currentDateTime(), {}, {{batchId, 3000000000LL}, {secondBatch, 7}}};
    LogisticsDialog dialog(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(dialog.initialize().ok());
    QVERIFY(dialog.setInput(value).ok());
    QCOMPARE(dialog.input().lines.size(), 2);
    QCOMPARE(dialog.input().lines.at(0).quantity, 3000000000LL);
    const auto created = logistics->create(value);
    QVERIFY(created.ok());
    LogisticsDialog view(*master, *queries, *logistics, DocumentType::Transfer);
    QVERIFY(view.initialize(created.value(), true).ok());
    QVERIFY(view.isReadOnly());
    QVERIFY(!view.findChild<QLineEdit *>(QStringLiteral("documentNo"))->isEnabled());
    QVERIFY(!view.findChild<QComboBox *>(QStringLiteral("sourceLocationId"))->isEnabled());
    QVERIFY(!view.findChild<QComboBox *>(QStringLiteral("destinationLocationId"))->isEnabled());
    QVERIFY(!view.findChild<QPushButton *>(QStringLiteral("addLineButton"))->isEnabled());
}

void DocumentDialogsTest::shortageSavesNegativeBalance()
{
    LogisticsDialog dialog(*master, *queries, *logistics, DocumentType::Outbound);
    QVERIFY(dialog.initialize().ok());
    QVERIFY(dialog.setInput({QStringLiteral("O-TOO-MUCH"), DocumentType::Outbound, destinationId,
        externalId, QDateTime::currentDateTime(), {}, {{batchId, 6000000000LL}}, personId}).ok());
    QVERIFY(dialog.save());
    QCOMPARE(queries->balance(batchId, destinationId).value(), qint64(-6000000000LL));
    QCOMPARE(queries->negativeBalanceCount().value(), qint64(1));
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
    const EntityId selected = action == QStringLiteral("create") ? 0 : batchId;
    InboundPage page(*master, *inbound);
    auto *table = page.findChild<QTableWidget *>(QStringLiteral("documents"));
    auto *error = page.findChild<QLabel *>(QStringLiteral("errorLabel"));
    QVERIFY(table && error);
    if (selected) {
        QVERIFY(page.selectBatch(selected).ok());
        QCOMPARE(table->item(table->currentRow(), 0)->data(Qt::UserRole + 1).toLongLong(), selected);
        QVERIFY(db->execute(QStringLiteral("ALTER TABLE inbound_batches RENAME TO unavailable_inbound_batches")));
        QVERIFY(QMetaObject::invokeMethod(&page, action == QStringLiteral("edit") ? "editSelected" : "viewSelected"));
    } else {
        QVERIFY(db->execute(QStringLiteral("ALTER TABLE product_categories RENAME TO unavailable_categories")));
        page.findChild<QPushButton *>(QStringLiteral("createButton"))->click();
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
        const auto document = logistics->create({QStringLiteral("T-PAGE-INIT"), DocumentType::Transfer,
            sourceId, destinationId, QDateTime::currentDateTime(), {}, {{batchId, 1}}});
        QVERIFY(document.ok());
        selected = document.value();
    }
    LogisticsPage page(*master, *queries, *logistics, DocumentType::Transfer);
    auto *table = page.findChild<QTableWidget *>(QStringLiteral("documents"));
    auto *error = page.findChild<QLabel *>(QStringLiteral("errorLabel"));
    QVERIFY(table && error);
    if (selected) {
        table->selectRow(0);
        QCOMPARE(table->item(table->currentRow(), 0)->data(Qt::UserRole + 1).toLongLong(), selected);
        QVERIFY(db->execute(QStringLiteral("ALTER TABLE logistics_documents RENAME TO unavailable_logistics_documents")));
        QVERIFY(QMetaObject::invokeMethod(&page, action == QStringLiteral("edit") ? "editSelected" : "viewSelected"));
    } else {
        QVERIFY(db->execute(QStringLiteral("ALTER TABLE locations RENAME TO unavailable_locations")));
        page.findChild<QPushButton *>(QStringLiteral("createButton"))->click();
    }
    QVERIFY(!page.lastError().code.isEmpty());
    QVERIFY(!error->text().isEmpty());
    QCOMPARE(error->text(), page.lastError().message);
}

void DocumentDialogsTest::pagesExposeFailuresAndClearErrorsOnSuccess()
{
    const auto consumed = logistics->create({QStringLiteral("O-CONSUME"), DocumentType::Outbound,
        destinationId, externalId, QDateTime::currentDateTime(), {}, {{batchId, 1}}, personId});
    QVERIFY(consumed.ok());
    InboundPage inboundPage(*master, *inbound);
    QSignalSpy inboundChanged(&inboundPage, &InboundPage::dataChanged);
    auto *inboundError = inboundPage.findChild<QLabel *>(QStringLiteral("errorLabel"));
    inboundPage.setConfirmationCallback([](const QString &) { return true; });
    inboundPage.findChild<QTableWidget *>(QStringLiteral("documents"))->selectRow(0);
    QVERIFY(db->execute(QStringLiteral(
        "CREATE TRIGGER abort_cancel BEFORE UPDATE OF status ON inbound_batches "
        "BEGIN SELECT RAISE(ABORT,'forced cancellation failure'); END")));
    QVERIFY(QMetaObject::invokeMethod(&inboundPage, "cancelSelected"));
    QCOMPARE(inboundPage.lastError().code, QStringLiteral("database"));
    QCOMPARE(inboundError->text(), inboundPage.lastError().message);
    QCOMPARE(inboundChanged.count(), 0);
    QVERIFY(db->execute(QStringLiteral("DROP TRIGGER abort_cancel")));
    QVERIFY(QMetaObject::invokeMethod(&inboundPage, "cancelSelected"));
    QVERIFY(inboundError->text().isEmpty());
    QCOMPARE(inboundChanged.count(), 1);

    LogisticsPage logisticsPage(*master, *queries, *logistics, DocumentType::Outbound);
    QSignalSpy logisticsChanged(&logisticsPage, &LogisticsPage::dataChanged);
    auto *logisticsError = logisticsPage.findChild<QLabel *>(QStringLiteral("errorLabel"));
    logisticsPage.setConfirmationCallback([](const QString &) { return true; });
    logisticsPage.findChild<QTableWidget *>(QStringLiteral("documents"))->selectRow(0);
    QVERIFY(db->execute(QStringLiteral(
        "CREATE TRIGGER abort_logistics_cancel BEFORE UPDATE OF status ON logistics_documents "
        "BEGIN SELECT RAISE(ABORT,'forced cancellation failure'); END")));
    QVERIFY(QMetaObject::invokeMethod(&logisticsPage, "cancelSelected"));
    QCOMPARE(logisticsPage.lastError().code, QStringLiteral("database"));
    QCOMPARE(logisticsError->text(), logisticsPage.lastError().message);
    QCOMPARE(logisticsChanged.count(), 0);
}

void DocumentDialogsTest::logisticsPageCancellationAndDataChanged()
{
    const auto created = logistics->create({QStringLiteral("T-PAGE"), DocumentType::Transfer, sourceId,
        destinationId, QDateTime::currentDateTime(), {}, {{batchId, 1}}});
    QVERIFY(created.ok());
    LogisticsPage page(*master, *queries, *logistics, DocumentType::Transfer);
    QSignalSpy changed(&page, &LogisticsPage::dataChanged);
    int callbacks = 0;
    page.setConfirmationCallback([&callbacks](const QString &) { ++callbacks; return true; });
    page.findChild<QTableWidget *>(QStringLiteral("documents"))->selectRow(0);
    QVERIFY(QMetaObject::invokeMethod(&page, "cancelSelected"));
    QCOMPARE(callbacks, 1);
    QCOMPARE(changed.count(), 1);
    QVERIFY(page.lastError().code.isEmpty());
}

void DocumentDialogsTest::pageConfirmationAndDataChanged()
{
    InboundPage page(*master, *inbound);
    QSignalSpy changed(&page, &InboundPage::dataChanged);
    page.setConfirmationCallback([](const QString &) { return false; });
    page.findChild<QTableWidget *>(QStringLiteral("documents"))->selectRow(0);
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
    const auto second = inbound->create({QStringLiteral("B-OTHER"), otherType.value().id,
        10, sourceId, QDateTime::currentDateTime(), {}, QStringLiteral("V40"), personId});
    QVERIFY(second.ok());
    QVERIFY(logistics->create({QStringLiteral("T-MODELS"), DocumentType::Transfer, sourceId,
        destinationId, QDateTime::currentDateTime(), {}, {{batchId, 5000000000LL}}}).ok());
    LogisticsDialog outbound(*master, *queries, *logistics, DocumentType::Outbound);
    QVERIFY(outbound.initialize().ok());
    QVERIFY(outbound.setSourceLocation(destinationId).ok());
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

    LogisticsInput value{QStringLiteral("O-MODELS"), DocumentType::Outbound, destinationId, externalId,
        QDateTime::currentDateTime(), {}, {{batchId, 3000000000LL, f25.value().id},
                                         {batchId, 2000000000LL, p25.value().id}}, personId};
    QVERIFY(outbound.setInput(value).ok());
    QCOMPARE(outbound.input().lines.front().productModelId, f25.value().id);
    QVERIFY(outbound.save());
    const auto sent = logistics->list(DocumentType::Outbound);
    QVERIFY(sent.ok());
    QCOMPARE(sent.value().size(), 1);
    QCOMPARE(queries->batchRemaining(batchId).value(), qint64(0));

    LogisticsDialog returned(*master, *queries, *logistics, DocumentType::Return);
    QVERIFY(returned.initialize().ok());
    QVERIFY(returned.setSourceLocation(destinationId).ok());
    QVERIFY(returned.visibleBatchIds().contains(batchId));
    auto *destinations = returned.findChild<QComboBox *>(QStringLiteral("destinationLocationId"));
    QVERIFY(destinations->findData(sourceId) >= 0);
    QCOMPARE(destinations->findData(externalId), -1);
    QVERIFY(returned.setInput({QStringLiteral("R-MODELS"), DocumentType::Return, destinationId, sourceId,
        QDateTime::currentDateTime(), {}, {{batchId, 50, f25.value().id}}}).ok());
    QVERIFY(returned.save());
    const auto saved = logistics->list(DocumentType::Return);
    QVERIFY(saved.ok());
    QCOMPARE(saved.value().size(), 1);
    QCOMPARE(queries->batchRemaining(batchId).value(), qint64(0));
    LogisticsDialog view(*master, *queries, *logistics, DocumentType::Return);
    QVERIFY(view.initialize(saved.value().front().id, true).ok());
    auto *viewLines = view.findChild<QTableWidget *>(QStringLiteral("lines"));
    auto *viewModel = qobject_cast<QComboBox *>(viewLines->cellWidget(0, 2));
    QCOMPARE(viewModel->currentData().toLongLong(), f25.value().id);
    QVERIFY(!viewModel->isEnabled());

    QVERIFY(master->setProductModelEnabled(f25.value().id, false).ok());
    LogisticsDialog edit(*master, *queries, *logistics, DocumentType::Outbound);
    QVERIFY(edit.initialize(sent.value().front().id).ok());
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
