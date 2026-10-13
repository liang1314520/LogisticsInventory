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
    void draftDoesNotAffectInventory();
    void updateOnlyChangesDraft();
    void confirmPostsExactlyOneEntryAndSnapshotsMasterData();
    void confirmRejectsInvalidMasterDataAndRollsBack();
    void confirmRollsBackEntryWhenStatusUpdateFails();
    void confirmRollsBackSnapshotAndEntryWhenStatusUpdateFails();
    void cancelPostsReversalExactlyOnce();
    void cancelRejectsWhenReversalWouldMakeStockNegative();
    void cancelPropagatesQuantityQueryFailure();
    void copyOnlyAllowsCancelledAndSupportsRepeatedCopies();
    void repositoryWritesEmptyBatchNumberAsSqlNullOnUpdate();
    void listIsNewestFirstAndDeleteOnlyAllowsDraft();

private:
    InboundDraft validDraft(qint64 quantity = 100) const;
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

InboundDraft InboundServiceTest::validDraft(qint64 quantity) const
{
    return {QStringLiteral("B-20260910-001"), productTypeId, quantity, locationId,
            QDateTime::fromString(QStringLiteral("2026-09-10T09:30:00"), Qt::ISODate),
            QStringLiteral("首批")};
}

void InboundServiceTest::draftDoesNotAffectInventory()
{
    const auto created = inbound->createDraft(validDraft(820));
    QVERIFY(created.ok());
    QCOMPARE(inventory->quantity(created.value(), locationId), qint64(0));
    QVERIFY(inventory->entriesForSource(QStringLiteral("Inbound"), created.value()).isEmpty());
}

void InboundServiceTest::updateOnlyChangesDraft()
{
    const auto created = inbound->createDraft(validDraft());
    QVERIFY(created.ok());
    auto changed = validDraft(220);
    changed.note = QStringLiteral("修订");
    QVERIFY(inbound->updateDraft(created.value(), changed).ok());
    const auto saved = documents->inboundById(created.value());
    QVERIFY(saved.ok());
    QCOMPARE(saved.value().quantity, qint64(220));
    QCOMPARE(saved.value().note, QStringLiteral("修订"));

    QVERIFY(inbound->confirm(created.value()).ok());
    changed.quantity = 330;
    const auto rejected = inbound->updateDraft(created.value(), changed);
    QVERIFY(!rejected.ok());
    QCOMPARE(rejected.error().code, QStringLiteral("invalid_status"));
    QCOMPARE(inventory->quantity(created.value(), locationId), qint64(220));
}

void InboundServiceTest::confirmPostsExactlyOneEntryAndSnapshotsMasterData()
{
    const auto created = inbound->createDraft(validDraft(820));
    QVERIFY(created.ok());
    QVERIFY(inbound->confirm(created.value()).ok());
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

    const auto duplicate = inbound->confirm(created.value());
    QVERIFY(!duplicate.ok());
    QCOMPARE(duplicate.error().code, QStringLiteral("invalid_status"));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Inbound"), created.value()).size(), 1);
}

void InboundServiceTest::confirmRejectsInvalidMasterDataAndRollsBack()
{
    const auto created = inbound->createDraft(validDraft());
    QVERIFY(created.ok());
    QVERIFY(masterService->setProductTypeEnabled(productTypeId, false).ok());
    QVERIFY(masterService->productTypeById(productTypeId).ok());
    const auto rejected = inbound->confirm(created.value());
    QVERIFY(!rejected.ok());
    QCOMPARE(rejected.error().code, QStringLiteral("invalid_master_data"));
    QCOMPARE(documents->inboundById(created.value()).value().status, DocumentStatus::Draft);
    QVERIFY(inventory->entriesForSource(QStringLiteral("Inbound"), created.value()).isEmpty());
}

void InboundServiceTest::confirmRollsBackEntryWhenStatusUpdateFails()
{
    const auto created = inbound->createDraft(validDraft(100));
    QVERIFY(created.ok());
    QVERIFY(db->execute(QStringLiteral(
        "CREATE TRIGGER abort_inbound_confirm BEFORE UPDATE OF status ON inbound_batches "
        "WHEN NEW.id=%1 AND NEW.status='Confirmed' "
        "BEGIN SELECT RAISE(ABORT, 'forced confirm failure'); END")
                            .arg(created.value())));

    const auto rejected = inbound->confirm(created.value());
    QVERIFY(!rejected.ok());
    QCOMPARE(rejected.error().code, QStringLiteral("database"));
    QVERIFY(inventory->entriesForSource(QStringLiteral("Inbound"), created.value()).isEmpty());
    const auto batch = documents->inboundById(created.value());
    QVERIFY(batch.ok());
    QCOMPARE(batch.value().status, DocumentStatus::Draft);
}

void InboundServiceTest::confirmRollsBackSnapshotAndEntryWhenStatusUpdateFails()
{
    const auto created = inbound->createDraft(validDraft(100));
    QVERIFY(created.ok());
    QVERIFY(db->execute(QStringLiteral(
        "CREATE TRIGGER abort_inbound_snapshot BEFORE UPDATE OF category_name_snapshot "
        "ON inbound_batches WHEN NEW.id=%1 "
        "BEGIN SELECT RAISE(ABORT, 'forced snapshot failure'); END")
                            .arg(created.value())));
    const auto rejected = inbound->confirm(created.value());
    QVERIFY(!rejected.ok());
    QCOMPARE(rejected.error().code, QStringLiteral("database"));
    QVERIFY(inventory->entriesForSource(QStringLiteral("Inbound"), created.value()).isEmpty());
    const auto batch = documents->inboundById(created.value());
    QVERIFY(batch.ok());
    QCOMPARE(batch.value().status, DocumentStatus::Draft);
    QVERIFY(batch.value().categoryNameSnapshot.isEmpty());
    QVERIFY(batch.value().typeCodeSnapshot.isEmpty());
}

void InboundServiceTest::cancelPostsReversalExactlyOnce()
{
    const auto created = inbound->createDraft(validDraft(100));
    QVERIFY(created.ok());
    QVERIFY(inbound->confirm(created.value()).ok());
    QVERIFY(masterService->setCategoryEnabled(categoryId, false).ok());
    QVERIFY(masterService->productTypeById(productTypeId).ok());
    auto secondDraft = validDraft();
    secondDraft.batchNo = QStringLiteral("B-DISABLED-CATEGORY");
    const auto second = inbound->createDraft(secondDraft);
    QVERIFY(second.ok());
    const auto categoryRejected = inbound->confirm(second.value());
    QVERIFY(!categoryRejected.ok());
    QCOMPARE(categoryRejected.error().code, QStringLiteral("invalid_master_data"));
    QVERIFY(masterService->setCategoryEnabled(categoryId, true).ok());
    QVERIFY(inbound->cancel(created.value()).ok());
    QCOMPARE(inventory->quantity(created.value(), locationId), qint64(0));
    const auto entries = inventory->entriesForSource(QStringLiteral("Inbound"), created.value());
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(1).reversalOfEntryId, entries.at(0).id);
    QCOMPARE(entries.at(1).quantityDelta, qint64(-100));
    QCOMPARE(documents->inboundById(created.value()).value().status, DocumentStatus::Cancelled);

    const auto duplicate = inbound->cancel(created.value());
    QVERIFY(!duplicate.ok());
    QCOMPARE(duplicate.error().code, QStringLiteral("invalid_status"));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Inbound"), created.value()).size(), 2);
}

void InboundServiceTest::cancelRejectsWhenReversalWouldMakeStockNegative()
{
    const auto created = inbound->createDraft(validDraft(100));
    QVERIFY(created.ok());
    QVERIFY(inbound->confirm(created.value()).ok());

    QSqlQuery consume(db->database().connection());
    consume.prepare(QStringLiteral(
        "INSERT INTO inventory_entries(batch_id,location_id,source_type,source_id,"
        "quantity_delta,business_at,created_at) VALUES(?,?,'Logistics',?,-1,?,?)"));
    consume.addBindValue(created.value());
    consume.addBindValue(locationId);
    consume.addBindValue(9001);
    consume.addBindValue(QStringLiteral("2026-09-10T10:00:00"));
    consume.addBindValue(QStringLiteral("2026-09-10T10:00:00"));
    QVERIFY(consume.exec());

    const auto rejected = inbound->cancel(created.value());
    QVERIFY(!rejected.ok());
    QCOMPARE(rejected.error().code, QStringLiteral("stock_dependency"));
    QCOMPARE(inventory->quantity(created.value(), locationId), qint64(99));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Inbound"), created.value()).size(), 1);
    QCOMPARE(documents->inboundById(created.value()).value().status, DocumentStatus::Confirmed);
}

void InboundServiceTest::cancelPropagatesQuantityQueryFailure()
{
    const auto created = inbound->createDraft(validDraft(100));
    QVERIFY(created.ok());
    QVERIFY(inbound->confirm(created.value()).ok());

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
    const auto batch = documents->inboundById(created.value());
    QVERIFY(batch.ok());
    QCOMPARE(batch.value().status, DocumentStatus::Confirmed);
}

void InboundServiceTest::copyOnlyAllowsCancelledAndSupportsRepeatedCopies()
{
    const auto created = inbound->createDraft(validDraft());
    QVERIFY(created.ok());
    QCOMPARE(inbound->copyToDraft(created.value()).error().code,
             QStringLiteral("invalid_status"));
    QVERIFY(inbound->confirm(created.value()).ok());
    QCOMPARE(inbound->copyToDraft(created.value()).error().code,
             QStringLiteral("invalid_status"));
    QVERIFY(inbound->cancel(created.value()).ok());

    const auto firstCopy = inbound->copyToDraft(created.value());
    const auto secondCopy = inbound->copyToDraft(created.value());
    QVERIFY(firstCopy.ok());
    QVERIFY(secondCopy.ok());
    QVERIFY(firstCopy.value() != secondCopy.value());
    for (const EntityId copiedId : {firstCopy.value(), secondCopy.value()}) {
        const auto saved = documents->inboundById(copiedId);
        QVERIFY(saved.ok());
        QCOMPARE(saved.value().batchNo, QString());
        QCOMPARE(saved.value().status, DocumentStatus::Draft);
        QVERIFY(!saved.value().confirmedAt.isValid());
        QVERIFY(!saved.value().cancelledAt.isValid());
        QVERIFY(inventory->entriesForSource(QStringLiteral("Inbound"), copiedId).isEmpty());
        QCOMPARE(inbound->copyToDraft(copiedId).error().code,
                 QStringLiteral("invalid_status"));
    }
}

void InboundServiceTest::repositoryWritesEmptyBatchNumberAsSqlNullOnUpdate()
{
    const auto created = inbound->createDraft(validDraft());
    QVERIFY(created.ok());
    auto copiedDraft = validDraft();
    copiedDraft.batchNo.clear();
    QVERIFY(documents->updateInboundDraft(created.value(), copiedDraft).ok());
    QSqlQuery query(db->database().connection());
    query.prepare(QStringLiteral(
        "SELECT batch_no IS NULL FROM inbound_batches WHERE id=?"));
    query.addBindValue(created.value());
    QVERIFY(query.exec());
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
}

void InboundServiceTest::listIsNewestFirstAndDeleteOnlyAllowsDraft()
{
    auto older = validDraft();
    older.batchNo = QStringLiteral("B-OLD");
    older.businessAt = QDateTime::fromString(QStringLiteral("2026-08-01T09:00:00"), Qt::ISODate);
    auto newer = validDraft();
    newer.batchNo = QStringLiteral("B-NEW");
    newer.businessAt = QDateTime::fromString(QStringLiteral("2026-09-01T09:00:00"), Qt::ISODate);
    const auto oldId = inbound->createDraft(older);
    const auto newId = inbound->createDraft(newer);
    QVERIFY(oldId.ok()); QVERIFY(newId.ok());
    QVERIFY(inbound->confirm(newId.value()).ok());
    const auto listed = inbound->list();
    QVERIFY(listed.ok()); QCOMPARE(listed.value().size(), 2);
    QCOMPARE(listed.value().at(0).id, newId.value());
    const auto confirmedDelete = inbound->deleteDraft(newId.value());
    QVERIFY(!confirmedDelete.ok());
    QCOMPARE(confirmedDelete.error().code, QStringLiteral("invalid_status"));
    QVERIFY(inbound->cancel(newId.value()).ok());
    QCOMPARE(inbound->deleteDraft(newId.value()).error().code,
             QStringLiteral("invalid_status"));
    QVERIFY(inbound->deleteDraft(oldId.value()).ok());
    QCOMPARE(inbound->list().value().size(), 1);
}

QTEST_MAIN(InboundServiceTest)
#include "tst_inbound_service.moc"
