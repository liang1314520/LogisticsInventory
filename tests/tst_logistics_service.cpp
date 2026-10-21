#include "repositories/document_repository.h"
#include "repositories/inventory_repository.h"
#include "repositories/master_data_repository.h"
#include "services/inbound_service.h"
#include "services/logistics_service.h"
#include "services/master_data_service.h"
#include "test_support.h"

#include <limits>
#include <memory>
#include <QSqlQuery>
#include <QtTest>

class LogisticsServiceTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void duplicateBatchModelRollsBack();
    void updateReplacesInventory();
    void loadedDocumentLinesAreValidatedAndProcessed();
    void transferPostsBothSidesAndSnapshots();
    void confirmedHistoryKeepsNameAndCodeSnapshots();
    void outboundPostsSourceOnly();
    void validatesLocationsAndMaterialUsage();
    void cancelReversesAndAllowsNegativeStock();
    void duplicateTransitionsDoNotPostAgain();
    void repeatedRestoresKeepOneRecordAndReplaceEntries();
    void updateKeepsOriginalNumberWhenInputIsEmpty();
    void statusFailureRollsBackRealTransaction();
    void snapshotFailureRollsBackEntriesAndStatus();
    void quantityFailureIsPropagated();
    void cancelQuantityFailureIsPropagated();
    void listIsNewestFirstAndRetainsCancelledRecords();
private:
    EntityId location(const QString &name, bool inventory, LocationType type = LocationType::Company);
    LogisticsInput input(DocumentType type, EntityId destination, qint64 quantity = 40) const;

    std::unique_ptr<TestDatabase> db;
    std::unique_ptr<MasterDataRepository> masterRepository;
    std::unique_ptr<MasterDataService> masterService;
    std::unique_ptr<DocumentRepository> documents;
    std::unique_ptr<InventoryRepository> inventory;
    std::unique_ptr<InboundService> inbound;
    std::unique_ptr<LogisticsService> logistics;
    EntityId productTypeId = 0;
    EntityId batchId = 0;
    EntityId sourceId = 0;
    EntityId destinationId = 0;
    EntityId externalId = 0;
    EntityId personId = 0;
};

void LogisticsServiceTest::init()
{
    db = std::make_unique<TestDatabase>();
    QVERIFY(db->openAndMigrate());
    masterRepository = std::make_unique<MasterDataRepository>(db->database());
    masterService = std::make_unique<MasterDataService>(*masterRepository);
    documents = std::make_unique<DocumentRepository>(db->database());
    inventory = std::make_unique<InventoryRepository>(db->database());
    inbound = std::make_unique<InboundService>(db->database(), *masterRepository, *documents, *inventory);
    logistics = std::make_unique<LogisticsService>(db->database(), *masterRepository, *documents, *inventory);
    const auto category = masterService->createCategory(QStringLiteral("设备"));
    QVERIFY(category.ok());
    const auto product = masterService->createProductType(category.value().id, QStringLiteral("GW-01"), {});
    QVERIFY(product.ok());
    productTypeId = product.value().id;
    sourceId = location(QStringLiteral("公司仓"), true);
    destinationId = location(QStringLiteral("工厂仓"), true, LocationType::Factory);
    externalId = location(QStringLiteral("Amazon"), false, LocationType::Other);
    const auto person = masterService->createPersonnel(QStringLiteral("经办人"));
    QVERIFY(person.ok());
    personId = person.value().id;
    const auto created = inbound->create({QStringLiteral("B-001"), productTypeId, 100, sourceId,
        QDateTime::fromString(QStringLiteral("2026-09-10T09:00:00"), Qt::ISODate), {},
        QStringLiteral("V40"), personId});
    QVERIFY(created.ok());
    batchId = created.value();
}

EntityId LogisticsServiceTest::location(const QString &name, bool inventory, LocationType type)
{
    const auto result = masterService->createLocation(name, type, inventory);
    return result.ok() ? result.value().id : 0;
}

LogisticsInput LogisticsServiceTest::input(DocumentType type, EntityId destination, qint64 quantity) const
{
    return {QStringLiteral("L-001"), type, type == DocumentType::Transfer ? sourceId : destinationId,
        destination, QDateTime::fromString(QStringLiteral("2026-09-10T10:00:00"), Qt::ISODate),
        QStringLiteral("发运"), {{batchId, quantity}}, type == DocumentType::Outbound ? personId : 0};
}

void LogisticsServiceTest::duplicateBatchModelRollsBack()
{
    auto value = input(DocumentType::Outbound, externalId, 60);
    value.lines.append({batchId, 50});
    const auto rejected = logistics->create(value);
    QVERIFY(!rejected.ok());
    QCOMPARE(rejected.error().code, QStringLiteral("duplicate_batch"));
    QCOMPARE(rejected.error().field, QStringLiteral("lines[1].batchId"));
    QCOMPARE(inventory->quantity(batchId, sourceId), qint64(100));
}

void LogisticsServiceTest::updateReplacesInventory()
{
    const auto created = logistics->create(input(DocumentType::Transfer, destinationId, 40));
    QVERIFY(created.ok());
    auto changed = input(DocumentType::Transfer, destinationId, 30);
    changed.documentNo = QStringLiteral("L-UPDATED");
    changed.note = QStringLiteral("更新");
    QVERIFY(logistics->update(created.value(), changed).ok());
    const auto saved = documents->logisticsById(created.value());
    QVERIFY(saved.ok());
    QCOMPARE(saved.value().type, DocumentType::Transfer);
    QCOMPARE(saved.value().lines.front().quantity, qint64(30));
    QCOMPARE(saved.value().note, QStringLiteral("更新"));
    QCOMPARE(inventory->quantity(batchId, sourceId), qint64(70));
    QCOMPARE(inventory->quantity(batchId, destinationId), qint64(30));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Logistics"), created.value()).size(), 2);
}

void LogisticsServiceTest::loadedDocumentLinesAreValidatedAndProcessed()
{
    const auto created = logistics->create(input(DocumentType::Transfer, destinationId));
    QVERIFY(created.ok());
    const auto loaded = documents->logisticsById(created.value());
    QVERIFY(loaded.ok());
    QCOMPARE(loaded.value().lines.size(), 1);
    QCOMPARE(loaded.value().lines.front().batchId, batchId);
    QCOMPARE(inventory->quantity(batchId, sourceId), qint64(60));
    QCOMPARE(inventory->quantity(batchId, destinationId), qint64(40));
}

void LogisticsServiceTest::transferPostsBothSidesAndSnapshots()
{
    const auto created = logistics->create(input(DocumentType::Transfer, destinationId));
    QVERIFY(created.ok());
    QCOMPARE(inventory->quantity(batchId, sourceId), qint64(60));
    QCOMPARE(inventory->quantity(batchId, destinationId), qint64(40));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Logistics"), created.value()).size(), 2);
    const auto saved = documents->logisticsById(created.value());
    QVERIFY(saved.ok());
    QCOMPARE(saved.value().status, DocumentStatus::Confirmed);
    QCOMPARE(saved.value().sourceNameSnapshot, QStringLiteral("公司仓"));
    QCOMPARE(saved.value().destinationNameSnapshot, QStringLiteral("工厂仓"));
    QCOMPARE(saved.value().lines.front().batchNoSnapshot, QStringLiteral("B-001"));
    QCOMPARE(saved.value().lines.front().productTypeId, productTypeId);
    QCOMPARE(saved.value().lines.front().categoryNameSnapshot, QStringLiteral("设备"));
    QCOMPARE(saved.value().lines.front().typeCodeSnapshot, QStringLiteral("GW-01"));
}

void LogisticsServiceTest::confirmedHistoryKeepsNameAndCodeSnapshots()
{
    const auto created = logistics->create(input(DocumentType::Transfer, destinationId));
    QVERIFY(created.ok());
    QSqlQuery rename(db->database().connection());
    rename.prepare(QStringLiteral("UPDATE product_categories SET name='新设备' WHERE id="
                                  "(SELECT category_id FROM product_types WHERE id=?)"));
    rename.addBindValue(productTypeId);
    QVERIFY(rename.exec());
    rename.prepare(QStringLiteral("UPDATE product_types SET type_code='GW-NEW' WHERE id=?"));
    rename.addBindValue(productTypeId);
    QVERIFY(rename.exec());
    const auto saved = documents->logisticsById(created.value());
    QVERIFY(saved.ok());
    QCOMPARE(saved.value().lines.front().productTypeId, productTypeId);
    QCOMPARE(saved.value().lines.front().categoryNameSnapshot, QStringLiteral("设备"));
    QCOMPARE(saved.value().lines.front().typeCodeSnapshot, QStringLiteral("GW-01"));
}

void LogisticsServiceTest::outboundPostsSourceOnly()
{
    const auto created = logistics->create(input(DocumentType::Outbound, externalId));
    QVERIFY(created.ok());
    QCOMPARE(inventory->quantity(batchId, sourceId), qint64(100));
    QCOMPARE(inventory->quantity(batchId, destinationId), qint64(-40));
    QCOMPARE(inventory->quantity(batchId, externalId), qint64(0));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Logistics"), created.value()).size(), 1);
}

void LogisticsServiceTest::validatesLocationsAndMaterialUsage()
{
    auto empty = input(DocumentType::Transfer, destinationId);
    empty.lines.clear();
    QCOMPARE(logistics->create(empty).error().field, QStringLiteral("lines"));
    const auto invalidQuantity = logistics->create(input(DocumentType::Transfer, destinationId, 0));
    QVERIFY(!invalidQuantity.ok());
    QCOMPARE(invalidQuantity.error().field, QStringLiteral("lines[0].quantity"));
    auto invalidBatch = input(DocumentType::Transfer, destinationId);
    invalidBatch.lines.front().batchId = 0;
    QCOMPARE(logistics->create(invalidBatch).error().field, QStringLiteral("lines[0].batchId"));
    QCOMPARE(logistics->create(input(DocumentType::Transfer, sourceId)).error().code, QStringLiteral("invalid"));
    QCOMPARE(logistics->create(input(DocumentType::Transfer, externalId)).error().field,
             QStringLiteral("destinationLocationId"));
    QCOMPARE(logistics->create(input(DocumentType::Outbound, sourceId)).error().field,
             QStringLiteral("destinationLocationId"));
}

void LogisticsServiceTest::cancelReversesAndAllowsNegativeStock()
{
    const auto created = logistics->create(input(DocumentType::Transfer, destinationId));
    QVERIFY(created.ok());
    QVERIFY(logistics->cancel(created.value()).ok());
    QCOMPARE(inventory->quantity(batchId, sourceId), qint64(100));
    QCOMPARE(inventory->quantity(batchId, destinationId), qint64(0));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Logistics"), created.value()).size(), 4);
    auto second = input(DocumentType::Transfer, destinationId);
    second.documentNo = QStringLiteral("L-002");
    const auto secondId = logistics->create(second);
    QVERIFY(secondId.ok());
    QVERIFY(inventory->insertEntry(batchId, destinationId, QStringLiteral("Logistics"),
                                   9999, 0, -1, second.businessAt).ok());
    QVERIFY(logistics->cancel(secondId.value()).ok());
    QCOMPARE(inventory->quantity(batchId, destinationId), qint64(-1));
    QCOMPARE(documents->logisticsById(secondId.value()).value().status, DocumentStatus::Cancelled);
}

void LogisticsServiceTest::duplicateTransitionsDoNotPostAgain()
{
    const auto created = logistics->create(input(DocumentType::Outbound, externalId));
    QVERIFY(created.ok());
    QVERIFY(logistics->update(created.value(), input(DocumentType::Outbound, externalId)).ok());
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Logistics"), created.value()).size(), 1);
    QVERIFY(logistics->cancel(created.value()).ok());
    QVERIFY(logistics->cancel(created.value()).ok());
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Logistics"), created.value()).size(), 2);
}

void LogisticsServiceTest::repeatedRestoresKeepOneRecordAndReplaceEntries()
{
    const auto created = logistics->create(input(DocumentType::Transfer, destinationId));
    QVERIFY(created.ok());
    const auto original = documents->logisticsById(created.value()).value();
    for (int i = 0; i < 2; ++i) {
        QVERIFY(logistics->cancel(created.value()).ok());
        QVERIFY(logistics->update(created.value(), input(DocumentType::Transfer, destinationId)).ok());
        const auto saved = documents->logisticsById(created.value()).value();
        QCOMPARE(saved.id, created.value());
        QCOMPARE(saved.createdAt, original.createdAt);
        QCOMPARE(saved.status, DocumentStatus::Confirmed);
        QVERIFY(!saved.cancelledAt.isValid());
        QCOMPARE(saved.lines.size(), 1);
        QCOMPARE(inventory->entriesForSource(QStringLiteral("Logistics"), created.value()).size(), 2);
        QCOMPARE(inventory->quantity(batchId, sourceId), qint64(60));
    }
    QCOMPARE(logistics->list(DocumentType::Transfer).value().size(), 1);
}

void LogisticsServiceTest::updateKeepsOriginalNumberWhenInputIsEmpty()
{
    const auto created = logistics->create(input(DocumentType::Transfer, destinationId));
    QVERIFY(created.ok());
    auto changed = input(DocumentType::Transfer, destinationId);
    changed.documentNo.clear();
    QVERIFY(logistics->update(created.value(), changed).ok());
    QCOMPARE(documents->logisticsById(created.value()).value().documentNo, QStringLiteral("L-001"));
}

void LogisticsServiceTest::statusFailureRollsBackRealTransaction()
{
    const auto created = logistics->create(input(DocumentType::Transfer, destinationId));
    QVERIFY(created.ok());
    QVERIFY(db->execute(QStringLiteral(
        "CREATE TRIGGER abort_logistics_update BEFORE UPDATE OF status ON logistics_documents "
        "WHEN NEW.id=%1 AND NEW.status='Confirmed' "
        "BEGIN SELECT RAISE(ABORT, 'forced update failure'); END").arg(created.value())));
    const auto rejected = logistics->update(created.value(), input(DocumentType::Transfer, destinationId, 50));
    QVERIFY(!rejected.ok());
    QCOMPARE(rejected.error().code, QStringLiteral("database"));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Logistics"), created.value()).size(), 2);
    QCOMPARE(inventory->quantity(batchId, sourceId), qint64(60));
    QCOMPARE(documents->logisticsById(created.value()).value().lines.front().quantity, qint64(40));
}

void LogisticsServiceTest::snapshotFailureRollsBackEntriesAndStatus()
{
    const auto created = logistics->create(input(DocumentType::Transfer, destinationId));
    QVERIFY(created.ok());
    const auto before = documents->logisticsById(created.value()).value();
    QVERIFY(db->execute(QStringLiteral(
        "CREATE TRIGGER abort_line_snapshot BEFORE INSERT ON logistics_document_lines "
        "WHEN NEW.document_id=%1 BEGIN SELECT RAISE(ABORT, 'forced snapshot failure'); END")
                            .arg(created.value())));
    const auto rejected = logistics->update(created.value(), input(DocumentType::Transfer, destinationId, 50));
    QVERIFY(!rejected.ok());
    QCOMPARE(rejected.error().code, QStringLiteral("database"));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Logistics"), created.value()).size(), 2);
    const auto after = documents->logisticsById(created.value());
    QVERIFY(after.ok());
    QCOMPARE(after.value().status, DocumentStatus::Confirmed);
    QCOMPARE(after.value().lines.front().id, before.lines.front().id);
    QCOMPARE(after.value().lines.front().batchNoSnapshot, QStringLiteral("B-001"));
}

void LogisticsServiceTest::quantityFailureIsPropagated()
{
    QSqlQuery overflow(db->database().connection());
    overflow.prepare(QStringLiteral(
        "INSERT INTO inventory_entries(batch_id,location_id,source_type,source_id,"
        "quantity_delta,business_at,created_at) VALUES(?,?,'Logistics',?,?,?,?)"));
    overflow.addBindValue(batchId);
    overflow.addBindValue(sourceId);
    overflow.addBindValue(9002);
    overflow.addBindValue(std::numeric_limits<qint64>::max());
    overflow.addBindValue(QStringLiteral("2026-09-10T10:00:00"));
    overflow.addBindValue(QStringLiteral("2026-09-10T10:00:00"));
    QVERIFY(overflow.exec());
    const auto rejected = logistics->create(input(DocumentType::Transfer, destinationId));
    QVERIFY(!rejected.ok());
    QCOMPARE(rejected.error().code, QStringLiteral("database"));
    QVERIFY(logistics->list(DocumentType::Transfer).value().isEmpty());
}

void LogisticsServiceTest::cancelQuantityFailureIsPropagated()
{
    const auto created = logistics->create(input(DocumentType::Transfer, destinationId));
    QVERIFY(created.ok());
    QSqlQuery overflow(db->database().connection());
    overflow.prepare(QStringLiteral(
        "INSERT INTO inventory_entries(batch_id,location_id,source_type,source_id,"
        "quantity_delta,business_at,created_at) VALUES(?,?,'Logistics',?,?,?,?)"));
    overflow.addBindValue(batchId);
    overflow.addBindValue(destinationId);
    overflow.addBindValue(9003);
    overflow.addBindValue(std::numeric_limits<qint64>::max());
    overflow.addBindValue(QStringLiteral("2026-09-10T10:30:00"));
    overflow.addBindValue(QStringLiteral("2026-09-10T10:30:00"));
    QVERIFY(overflow.exec());
    const auto rejected = logistics->cancel(created.value());
    QVERIFY(!rejected.ok());
    QCOMPARE(rejected.error().code, QStringLiteral("database"));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Logistics"), created.value()).size(), 2);
    QCOMPARE(documents->logisticsById(created.value()).value().status, DocumentStatus::Confirmed);
}

void LogisticsServiceTest::listIsNewestFirstAndRetainsCancelledRecords()
{
    auto older = input(DocumentType::Transfer, destinationId);
    older.documentNo = QStringLiteral("L-OLD");
    older.businessAt = QDateTime::fromString(QStringLiteral("2026-08-01T10:00:00"), Qt::ISODate);
    auto newer = input(DocumentType::Transfer, destinationId);
    newer.documentNo = QStringLiteral("L-NEW");
    newer.businessAt = QDateTime::fromString(QStringLiteral("2026-09-01T10:00:00"), Qt::ISODate);
    const auto oldId = logistics->create(older);
    const auto newId = logistics->create(newer);
    QVERIFY(oldId.ok() && newId.ok());
    const auto listed = logistics->list(DocumentType::Transfer);
    QVERIFY(listed.ok());
    QCOMPARE(listed.value().size(), 2);
    QCOMPARE(listed.value().at(0).id, newId.value());
    QVERIFY(logistics->cancel(newId.value()).ok());
    QVERIFY(logistics->cancel(oldId.value()).ok());
    QCOMPARE(logistics->list(DocumentType::Transfer).value().size(), 2);
}

QTEST_MAIN(LogisticsServiceTest)
#include "tst_logistics_service.moc"
