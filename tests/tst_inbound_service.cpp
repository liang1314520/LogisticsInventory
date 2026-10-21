#include "repositories/document_repository.h"
#include "repositories/inventory_repository.h"
#include "repositories/master_data_repository.h"
#include "services/inbound_service.h"
#include "services/master_data_service.h"
#include "test_support.h"

#include <limits>
#include <memory>
#include <QSqlQuery>
#include <QtTest>

class InboundServiceTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void saveImmediatelyAffectsInventory();
    void updateReplacesInventory();
    void savePostsExactlyOneEntryAndSnapshotsMasterData();
    void saveRejectsInvalidMasterDataAndRollsBack();
    void updateRollsBackEntryWhenStatusUpdateFails();
    void updateRollsBackSnapshotAndEntryWhenSnapshotUpdateFails();
    void cancelPostsReversalExactlyOnce();
    void cancelAllowsNegativeStock();
    void cancelPropagatesQuantityQueryFailure();
    void cancelledRecordCanBeRestoredRepeatedly();
    void updateKeepsOriginalNumberWhenInputIsEmpty();
    void listIsNewestFirstAndRetainsCancelledRecords();

private:
    InboundInput validInput(qint64 quantity = 100) const;
    EntityId createProductType(const QString &code = QStringLiteral("GW-01"));
    EntityId createLocation(const QString &name = QStringLiteral("公司仓"),
                            bool countsAsInventory = true);

    std::unique_ptr<TestDatabase> db;
    std::unique_ptr<MasterDataRepository> masterRepository;
    std::unique_ptr<MasterDataService> masterService;
    std::unique_ptr<DocumentRepository> documents;
    std::unique_ptr<InventoryRepository> inventory;
    std::unique_ptr<InboundService> inbound;
    EntityId categoryId = 0;
    EntityId productTypeId = 0;
    EntityId locationId = 0;
    EntityId personId = 0;
};

void InboundServiceTest::init()
{
    db = std::make_unique<TestDatabase>();
    QVERIFY(db->openAndMigrate());
    masterRepository = std::make_unique<MasterDataRepository>(db->database());
    masterService = std::make_unique<MasterDataService>(*masterRepository);
    documents = std::make_unique<DocumentRepository>(db->database());
    inventory = std::make_unique<InventoryRepository>(db->database());
    inbound = std::make_unique<InboundService>(db->database(), *masterRepository,
                                               *documents, *inventory);
    const auto category = masterService->createCategory(QStringLiteral("设备"));
    QVERIFY(category.ok());
    categoryId = category.value().id;
    productTypeId = createProductType();
    locationId = createLocation();
    const auto person = masterService->createPersonnel(QStringLiteral("签收人"));
    QVERIFY(person.ok());
    personId = person.value().id;
}

EntityId InboundServiceTest::createProductType(const QString &code)
{
    const auto type = masterService->createProductType(categoryId, code, QString());
    return type.ok() ? type.value().id : 0;
}

EntityId InboundServiceTest::createLocation(const QString &name, bool countsAsInventory)
{
    const auto location = masterService->createLocation(name, LocationType::Company,
                                                        countsAsInventory);
    return location.ok() ? location.value().id : 0;
}

InboundInput InboundServiceTest::validInput(qint64 quantity) const
{
    return {QStringLiteral("B-20260910-001"), productTypeId, quantity, locationId,
            QDateTime::fromString(QStringLiteral("2026-09-10T09:30:00"), Qt::ISODate),
            QStringLiteral("首批"), QStringLiteral("V40"), personId};
}

void InboundServiceTest::saveImmediatelyAffectsInventory()
{
    const auto created = inbound->create(validInput(820));
    QVERIFY(created.ok());
    QCOMPARE(inventory->quantity(created.value(), locationId), qint64(820));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Inbound"), created.value()).size(), 1);
}

void InboundServiceTest::updateReplacesInventory()
{
    const auto created = inbound->create(validInput());
    QVERIFY(created.ok());
    const auto original = documents->inboundById(created.value()).value();
    auto changed = validInput(220);
    changed.note = QStringLiteral("修订");
    QVERIFY(inbound->update(created.value(), changed).ok());
    const auto saved = documents->inboundById(created.value());
    QVERIFY(saved.ok());
    QCOMPARE(saved.value().quantity, qint64(220));
    QCOMPARE(saved.value().note, QStringLiteral("修订"));
    QCOMPARE(saved.value().createdAt, original.createdAt);
    changed.quantity = 330;
    QVERIFY(inbound->update(created.value(), changed).ok());
    QCOMPARE(inventory->quantity(created.value(), locationId), qint64(330));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Inbound"), created.value()).size(), 1);
}

void InboundServiceTest::savePostsExactlyOneEntryAndSnapshotsMasterData()
{
    const auto created = inbound->create(validInput(820));
    QVERIFY(created.ok());
    QCOMPARE(inventory->quantity(created.value(), locationId), qint64(820));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Inbound"), created.value()).size(), 1);
    const auto batch = documents->inboundById(created.value());
    QVERIFY(batch.ok());
    QCOMPARE(batch.value().status, DocumentStatus::Confirmed);
    QCOMPARE(batch.value().categoryNameSnapshot, QStringLiteral("设备"));
    QCOMPARE(batch.value().typeCodeSnapshot, QStringLiteral("GW-01"));
    QCOMPARE(batch.value().locationNameSnapshot, QStringLiteral("公司仓"));
    QVERIFY(batch.value().confirmedAt.isValid());
    QVERIFY(masterService->updateCategory(categoryId, QStringLiteral("新设备")).ok());
    QVERIFY(masterService->updateProductType(productTypeId, categoryId,
                                              QStringLiteral("GW-NEW"), QString()).ok());
    const auto historical = documents->inboundById(created.value());
    QVERIFY(historical.ok());
    QCOMPARE(historical.value().categoryNameSnapshot, QStringLiteral("设备"));
    QCOMPARE(historical.value().typeCodeSnapshot, QStringLiteral("GW-01"));
    QVERIFY(inbound->update(created.value(), validInput(820)).ok());
    QCOMPARE(documents->inboundById(created.value()).value().typeCodeSnapshot, QStringLiteral("GW-NEW"));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Inbound"), created.value()).size(), 1);
}

void InboundServiceTest::saveRejectsInvalidMasterDataAndRollsBack()
{
    QVERIFY(masterService->setProductTypeEnabled(productTypeId, false).ok());
    const auto rejected = inbound->create(validInput());
    QVERIFY(!rejected.ok());
    QCOMPARE(rejected.error().code, QStringLiteral("invalid_master_data"));
    QVERIFY(documents->inboundList().value().isEmpty());
    QCOMPARE(db->scalar(QStringLiteral("SELECT COUNT(*) FROM inventory_entries")).toInt(), 0);
}

void InboundServiceTest::updateRollsBackEntryWhenStatusUpdateFails()
{
    const auto created = inbound->create(validInput(100));
    QVERIFY(created.ok());
    QVERIFY(db->execute(QStringLiteral(
        "CREATE TRIGGER abort_inbound_update BEFORE UPDATE OF status ON inbound_batches "
        "WHEN NEW.id=%1 AND NEW.status='Confirmed' "
        "BEGIN SELECT RAISE(ABORT, 'forced update failure'); END").arg(created.value())));
    const auto rejected = inbound->update(created.value(), validInput(200));
    QVERIFY(!rejected.ok());
    QCOMPARE(rejected.error().code, QStringLiteral("database"));
    QCOMPARE(inventory->quantity(created.value(), locationId), qint64(100));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Inbound"), created.value()).size(), 1);
    QCOMPARE(documents->inboundById(created.value()).value().quantity, qint64(100));
}

void InboundServiceTest::updateRollsBackSnapshotAndEntryWhenSnapshotUpdateFails()
{
    const auto created = inbound->create(validInput(100));
    QVERIFY(created.ok());
    QVERIFY(masterService->updateCategory(categoryId, QStringLiteral("新设备")).ok());
    QVERIFY(db->execute(QStringLiteral(
        "CREATE TRIGGER abort_inbound_snapshot BEFORE UPDATE OF category_name_snapshot "
        "ON inbound_batches WHEN NEW.id=%1 "
        "BEGIN SELECT RAISE(ABORT, 'forced snapshot failure'); END").arg(created.value())));
    const auto rejected = inbound->update(created.value(), validInput(200));
    QVERIFY(!rejected.ok());
    QCOMPARE(rejected.error().code, QStringLiteral("database"));
    QCOMPARE(inventory->quantity(created.value(), locationId), qint64(100));
    const auto batch = documents->inboundById(created.value());
    QVERIFY(batch.ok());
    QCOMPARE(batch.value().status, DocumentStatus::Confirmed);
    QCOMPARE(batch.value().categoryNameSnapshot, QStringLiteral("设备"));
    QCOMPARE(batch.value().typeCodeSnapshot, QStringLiteral("GW-01"));
}

void InboundServiceTest::cancelPostsReversalExactlyOnce()
{
    const auto created = inbound->create(validInput(100));
    QVERIFY(created.ok());
    QVERIFY(masterService->setCategoryEnabled(categoryId, false).ok());
    auto secondInput = validInput();
    secondInput.batchNo = QStringLiteral("B-DISABLED-CATEGORY");
    const auto rejected = inbound->create(secondInput);
    QVERIFY(!rejected.ok());
    QCOMPARE(rejected.error().code, QStringLiteral("invalid_master_data"));
    QVERIFY(inbound->cancel(created.value()).ok());
    QCOMPARE(inventory->quantity(created.value(), locationId), qint64(0));
    const auto entries = inventory->entriesForSource(QStringLiteral("Inbound"), created.value());
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(1).reversalOfEntryId, entries.at(0).id);
    QCOMPARE(entries.at(1).quantityDelta, qint64(-100));
    QCOMPARE(documents->inboundById(created.value()).value().status, DocumentStatus::Cancelled);
    QVERIFY(inbound->cancel(created.value()).ok());
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Inbound"), created.value()).size(), 2);
}

void InboundServiceTest::cancelAllowsNegativeStock()
{
    const auto created = inbound->create(validInput(100));
    QVERIFY(created.ok());
    QVERIFY(inventory->insertEntry(created.value(), locationId, QStringLiteral("Logistics"),
                                   9001, 0, -1, validInput().businessAt).ok());
    QVERIFY(inbound->cancel(created.value()).ok());
    QCOMPARE(inventory->quantity(created.value(), locationId), qint64(-1));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Inbound"), created.value()).size(), 2);
    QCOMPARE(documents->inboundById(created.value()).value().status, DocumentStatus::Cancelled);
}

void InboundServiceTest::cancelPropagatesQuantityQueryFailure()
{
    const auto created = inbound->create(validInput(100));
    QVERIFY(created.ok());
    QSqlQuery overflow(db->database().connection());
    overflow.prepare(QStringLiteral(
        "INSERT INTO inventory_entries(batch_id,location_id,source_type,source_id,"
        "quantity_delta,business_at,created_at) VALUES(?,?,'Logistics',?,?,?,?)"));
    overflow.addBindValue(created.value());
    overflow.addBindValue(locationId);
    overflow.addBindValue(9002);
    overflow.addBindValue(std::numeric_limits<qint64>::max());
    overflow.addBindValue(QStringLiteral("2026-09-10T10:00:00"));
    overflow.addBindValue(QStringLiteral("2026-09-10T10:00:00"));
    QVERIFY(overflow.exec());
    const auto rejected = inbound->cancel(created.value());
    QVERIFY(!rejected.ok());
    QCOMPARE(rejected.error().code, QStringLiteral("database"));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Inbound"), created.value()).size(), 1);
    QCOMPARE(documents->inboundById(created.value()).value().status, DocumentStatus::Confirmed);
}

void InboundServiceTest::cancelledRecordCanBeRestoredRepeatedly()
{
    const auto created = inbound->create(validInput());
    QVERIFY(created.ok());
    const auto original = documents->inboundById(created.value()).value();
    for (int i = 0; i < 2; ++i) {
        QVERIFY(inbound->cancel(created.value()).ok());
        QVERIFY(inbound->update(created.value(), validInput()).ok());
        const auto saved = documents->inboundById(created.value());
        QVERIFY(saved.ok());
        QCOMPARE(saved.value().id, created.value());
        QCOMPARE(saved.value().createdAt, original.createdAt);
        QCOMPARE(saved.value().status, DocumentStatus::Confirmed);
        QVERIFY(!saved.value().cancelledAt.isValid());
        QCOMPARE(inventory->quantity(created.value(), locationId), qint64(100));
        QCOMPARE(inventory->entriesForSource(QStringLiteral("Inbound"), created.value()).size(), 1);
    }
    QCOMPARE(inbound->list().value().size(), 1);
}

void InboundServiceTest::updateKeepsOriginalNumberWhenInputIsEmpty()
{
    const auto created = inbound->create(validInput());
    QVERIFY(created.ok());
    auto changed = validInput();
    changed.batchNo.clear();
    QVERIFY(inbound->update(created.value(), changed).ok());
    QCOMPARE(documents->inboundById(created.value()).value().batchNo, validInput().batchNo);
}

void InboundServiceTest::listIsNewestFirstAndRetainsCancelledRecords()
{
    auto older = validInput();
    older.batchNo = QStringLiteral("B-OLD");
    older.businessAt = QDateTime::fromString(QStringLiteral("2026-08-01T09:00:00"), Qt::ISODate);
    auto newer = validInput();
    newer.batchNo = QStringLiteral("B-NEW");
    newer.businessAt = QDateTime::fromString(QStringLiteral("2026-09-01T09:00:00"), Qt::ISODate);
    const auto oldId = inbound->create(older);
    const auto newId = inbound->create(newer);
    QVERIFY(oldId.ok() && newId.ok());
    const auto listed = inbound->list();
    QVERIFY(listed.ok());
    QCOMPARE(listed.value().size(), 2);
    QCOMPARE(listed.value().at(0).id, newId.value());
    QVERIFY(inbound->cancel(newId.value()).ok());
    QVERIFY(inbound->cancel(oldId.value()).ok());
    QCOMPARE(inbound->list().value().size(), 2);
}

QTEST_MAIN(InboundServiceTest)
#include "tst_inbound_service.moc"
