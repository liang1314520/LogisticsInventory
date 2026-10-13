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
    void aggregateOverIssueRollsBack();
    void draftUpdateDoesNotPostInventory();
    void loadedDocumentLinesAreValidatedAndProcessed();
    void transferPostsBothSidesAndSnapshots();
    void confirmedHistoryKeepsNameAndCodeSnapshots();
    void outboundPostsSourceOnly();
    void validatesLocationsAndMaterialUsage();
    void cancelReversesAndRejectsStockDependency();
    void duplicateTransitionsDoNotPostAgain();
    void repeatedCopiesProduceCleanDraftsWithoutEntries();
    void repositoryWritesEmptyDocumentNumberAsSqlNullOnUpdate();
    void statusFailureRollsBackRealTransaction();
    void snapshotFailureRollsBackEntriesAndStatus();
    void quantityFailureIsPropagated();
    void cancelQuantityFailureIsPropagated();
    void listIsNewestFirstAndDeleteOnlyAllowsDraft();
private:
    EntityId location(const QString &name, bool inventory, LocationType type = LocationType::Company);
    LogisticsDraft draft(DocumentType type, EntityId destination, qint64 quantity = 40) const;

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
};

void LogisticsServiceTest::init()
{
    db = std::make_unique<TestDatabase>();
    QVERIFY(db->openAndMigrate());
    masterRepository = std::make_unique<MasterDataRepository>(db->database());
    masterService = std::make_unique<MasterDataService>(*masterRepository);
    documents = std::make_unique<DocumentRepository>(db->database());
    inventory = std::make_unique<InventoryRepository>(db->database());
    inbound = std::make_unique<InboundService>(db->database(), *masterRepository,
                                               *documents, *inventory);
    logistics = std::make_unique<LogisticsService>(db->database(), *masterRepository,
                                                   *documents, *inventory);
    const auto category = masterService->createCategory(QStringLiteral("设备"));
    QVERIFY(category.ok());
    const auto productType = masterService->createProductType(category.value().id,
                                                               QStringLiteral("GW-01"), QString());
    QVERIFY(productType.ok());
    productTypeId = productType.value().id;
    sourceId = location(QStringLiteral("公司仓"), true);
    destinationId = location(QStringLiteral("项目仓"), true, LocationType::ProjectSite);
    externalId = location(QStringLiteral("客户"), false, LocationType::Customer);
    const InboundDraft inboundDraft{QStringLiteral("B-001"), productTypeId, 100, sourceId,
                                    QDateTime::fromString(QStringLiteral("2026-09-10T09:00:00"), Qt::ISODate), {}};
    const auto inboundCreated = inbound->createDraft(inboundDraft);
    QVERIFY(inboundCreated.ok());
    batchId = inboundCreated.value();
    QVERIFY(inbound->confirm(batchId).ok());
}

EntityId LogisticsServiceTest::location(const QString &name, bool inventory, LocationType type)
{
    const auto result = masterService->createLocation(name, type, inventory);
    return result.ok() ? result.value().id : 0;
}

LogisticsDraft LogisticsServiceTest::draft(DocumentType type, EntityId destination,
                                            qint64 quantity) const
{
    return {QStringLiteral("L-001"), type, sourceId, destination,
            QDateTime::fromString(QStringLiteral("2026-09-10T10:00:00"), Qt::ISODate),
            QStringLiteral("发运"), {{batchId, quantity}}};
}

void LogisticsServiceTest::aggregateOverIssueRollsBack()
{
    auto value = draft(DocumentType::Outbound, externalId, 60);
    value.lines.append({batchId, 50});
    const auto rejected = logistics->createDraft(value);
    QVERIFY(!rejected.ok());
    QCOMPARE(rejected.error().code, QStringLiteral("duplicate_batch"));
    QCOMPARE(rejected.error().field, QStringLiteral("lines[1].batchId"));
    QCOMPARE(inventory->quantity(batchId, sourceId), qint64(100));
}

void LogisticsServiceTest::draftUpdateDoesNotPostInventory()
{
    const auto created = logistics->createDraft(draft(DocumentType::Transfer, destinationId, 40));
    QVERIFY(created.ok());
    auto changed = draft(DocumentType::Outbound, externalId, 30);
    changed.documentNo = QStringLiteral("L-UPDATED");
    changed.note = QStringLiteral("更新");
    QVERIFY(logistics->updateDraft(created.value(), changed).ok());
    const auto saved = documents->logisticsById(created.value());
    QVERIFY(saved.ok());
    QCOMPARE(saved.value().type, DocumentType::Outbound);
    QCOMPARE(saved.value().destinationLocationId, externalId);
    QCOMPARE(saved.value().lines.front().quantity, qint64(30));
    QVERIFY(inventory->entriesForSource(QStringLiteral("Logistics"), created.value()).isEmpty());
}

void LogisticsServiceTest::loadedDocumentLinesAreValidatedAndProcessed()
{
    const auto created = logistics->createDraft(draft(DocumentType::Transfer, destinationId));
    QVERIFY(created.ok());
    const auto loaded = documents->logisticsById(created.value());
    QVERIFY(loaded.ok());
    QCOMPARE(loaded.value().lines.size(), 1);
    QCOMPARE(loaded.value().lines.front().batchId, batchId);
    QVERIFY(logistics->confirm(created.value()).ok());
    QCOMPARE(inventory->quantity(batchId, sourceId), qint64(60));
    QCOMPARE(inventory->quantity(batchId, destinationId), qint64(40));
}

void LogisticsServiceTest::transferPostsBothSidesAndSnapshots()
{
    const auto created = logistics->createDraft(draft(DocumentType::Transfer, destinationId));
    QVERIFY(created.ok());
    QVERIFY(logistics->confirm(created.value()).ok());
    QCOMPARE(inventory->quantity(batchId, sourceId), qint64(60));
    QCOMPARE(inventory->quantity(batchId, destinationId), qint64(40));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Logistics"), created.value()).size(), 2);
    const auto saved = documents->logisticsById(created.value());
    QVERIFY(saved.ok());
    QCOMPARE(saved.value().status, DocumentStatus::Confirmed);
    QCOMPARE(saved.value().sourceNameSnapshot, QStringLiteral("公司仓"));
    QCOMPARE(saved.value().destinationNameSnapshot, QStringLiteral("项目仓"));
    QCOMPARE(saved.value().lines.front().batchNoSnapshot, QStringLiteral("B-001"));
    QCOMPARE(saved.value().lines.front().productTypeId, productTypeId);
    QCOMPARE(saved.value().lines.front().categoryNameSnapshot, QStringLiteral("设备"));
    QCOMPARE(saved.value().lines.front().typeCodeSnapshot, QStringLiteral("GW-01"));
}

void LogisticsServiceTest::confirmedHistoryKeepsNameAndCodeSnapshots()
{
    const auto created = logistics->createDraft(draft(DocumentType::Transfer, destinationId));
    QVERIFY(created.ok());
    QVERIFY(logistics->confirm(created.value()).ok());

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
    const auto created = logistics->createDraft(draft(DocumentType::Outbound, externalId));
    QVERIFY(created.ok());
    QVERIFY(logistics->confirm(created.value()).ok());
    QCOMPARE(inventory->quantity(batchId, sourceId), qint64(60));
    QCOMPARE(inventory->quantity(batchId, externalId), qint64(0));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Logistics"), created.value()).size(), 1);
}

void LogisticsServiceTest::validatesLocationsAndMaterialUsage()
{
    auto empty = draft(DocumentType::Transfer, destinationId);
    empty.lines.clear();
    QCOMPARE(logistics->createDraft(empty).error().code, QStringLiteral("required"));
    auto invalidQuantity = draft(DocumentType::Transfer, destinationId, 0);
    const auto invalidQuantityResult = logistics->createDraft(invalidQuantity);
    QCOMPARE(invalidQuantityResult.error().code, QStringLiteral("invalid"));
    QCOMPARE(invalidQuantityResult.error().field, QStringLiteral("lines[0].quantity"));
    auto invalidBatch = draft(DocumentType::Transfer, destinationId);
    invalidBatch.lines.front().batchId = 0;
    QCOMPARE(logistics->createDraft(invalidBatch).error().field, QStringLiteral("lines[0].batchId"));
    auto sameLocation = draft(DocumentType::Transfer, sourceId);
    QCOMPARE(logistics->createDraft(sameLocation).error().code, QStringLiteral("invalid"));
    auto transferExternal = draft(DocumentType::Transfer, externalId);
    const auto transferId = logistics->createDraft(transferExternal);
    QVERIFY(transferId.ok());
    QCOMPARE(logistics->confirm(transferId.value()).error().code,
             QStringLiteral("invalid_master_data"));
    auto outboundInventory = draft(DocumentType::Outbound, destinationId);
    outboundInventory.documentNo = QStringLiteral("L-002");
    const auto outboundId = logistics->createDraft(outboundInventory);
    QVERIFY(outboundId.ok());
    QCOMPARE(logistics->confirm(outboundId.value()).error().code,
             QStringLiteral("invalid_master_data"));
}

void LogisticsServiceTest::cancelReversesAndRejectsStockDependency()
{
    const auto created = logistics->createDraft(draft(DocumentType::Transfer, destinationId));
    QVERIFY(created.ok());
    QVERIFY(logistics->confirm(created.value()).ok());
    QVERIFY(logistics->cancel(created.value()).ok());
    QCOMPARE(inventory->quantity(batchId, sourceId), qint64(100));
    QCOMPARE(inventory->quantity(batchId, destinationId), qint64(0));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Logistics"), created.value()).size(), 4);

    auto second = draft(DocumentType::Transfer, destinationId);
    second.documentNo = QStringLiteral("L-002");
    const auto secondId = logistics->createDraft(second);
    QVERIFY(secondId.ok());
    QVERIFY(logistics->confirm(secondId.value()).ok());
    QVERIFY(inventory->insertEntry(batchId, destinationId, QStringLiteral("Logistics"),
                                   9999, 0, -1, second.businessAt).ok());
    const auto rejected = logistics->cancel(secondId.value());
    QVERIFY(!rejected.ok());
    QCOMPARE(rejected.error().code, QStringLiteral("stock_dependency"));
    QCOMPARE(documents->logisticsById(secondId.value()).value().status,
             DocumentStatus::Confirmed);
}

void LogisticsServiceTest::duplicateTransitionsDoNotPostAgain()
{
    const auto created = logistics->createDraft(draft(DocumentType::Outbound, externalId));
    QVERIFY(created.ok());
    QVERIFY(logistics->confirm(created.value()).ok());
    QCOMPARE(logistics->confirm(created.value()).error().code, QStringLiteral("invalid_status"));
    QVERIFY(logistics->cancel(created.value()).ok());
    QCOMPARE(logistics->cancel(created.value()).error().code, QStringLiteral("invalid_status"));
    QCOMPARE(inventory->entriesForSource(QStringLiteral("Logistics"), created.value()).size(), 2);
}

void LogisticsServiceTest::repeatedCopiesProduceCleanDraftsWithoutEntries()
{
    const auto created = logistics->createDraft(draft(DocumentType::Transfer, destinationId));
    QVERIFY(created.ok());
    QCOMPARE(logistics->copyToDraft(created.value()).error().code,
             QStringLiteral("invalid_status"));
    QVERIFY(logistics->confirm(created.value()).ok());
    QCOMPARE(logistics->copyToDraft(created.value()).error().code,
             QStringLiteral("invalid_status"));
    QVERIFY(logistics->cancel(created.value()).ok());
    const auto firstCopy = logistics->copyToDraft(created.value());
    const auto secondCopy = logistics->copyToDraft(created.value());
    QVERIFY(firstCopy.ok());
    QVERIFY(secondCopy.ok());
    QVERIFY(firstCopy.value() != secondCopy.value());
    for (const EntityId copiedId : {firstCopy.value(), secondCopy.value()}) {
        const auto saved = documents->logisticsById(copiedId);
        QVERIFY(saved.ok());
        QCOMPARE(saved.value().documentNo, QString());
        QCOMPARE(saved.value().status, DocumentStatus::Draft);
        QVERIFY(!saved.value().confirmedAt.isValid());
        QVERIFY(!saved.value().cancelledAt.isValid());
        QCOMPARE(saved.value().lines.size(), 1);
        QVERIFY(inventory->entriesForSource(QStringLiteral("Logistics"), copiedId).isEmpty());
        QCOMPARE(logistics->copyToDraft(copiedId).error().code,
                 QStringLiteral("invalid_status"));
    }
}

void LogisticsServiceTest::repositoryWritesEmptyDocumentNumberAsSqlNullOnUpdate()
{
    const auto created = logistics->createDraft(draft(DocumentType::Transfer, destinationId));
    QVERIFY(created.ok());
    auto copiedDraft = draft(DocumentType::Transfer, destinationId);
    copiedDraft.documentNo.clear();
    QVERIFY(documents->updateLogisticsDraft(created.value(), copiedDraft).ok());
    QSqlQuery query(db->database().connection());
    query.prepare(QStringLiteral(
        "SELECT document_no IS NULL FROM logistics_documents WHERE id=?"));
    query.addBindValue(created.value());
    QVERIFY(query.exec());
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
}

void LogisticsServiceTest::statusFailureRollsBackRealTransaction()
{
    const auto created = logistics->createDraft(draft(DocumentType::Transfer, destinationId));
    QVERIFY(created.ok());
    QVERIFY(db->execute(QStringLiteral(
        "CREATE TRIGGER abort_logistics_confirm BEFORE UPDATE OF status ON logistics_documents "
        "WHEN NEW.id=%1 AND NEW.status='Confirmed' "
        "BEGIN SELECT RAISE(ABORT, 'forced confirm failure'); END").arg(created.value())));
    const auto rejected = logistics->confirm(created.value());
    QVERIFY(!rejected.ok());
    QCOMPARE(rejected.error().code, QStringLiteral("database"));
    QVERIFY(inventory->entriesForSource(QStringLiteral("Logistics"), created.value()).isEmpty());
    QCOMPARE(documents->logisticsById(created.value()).value().status, DocumentStatus::Draft);
}

void LogisticsServiceTest::snapshotFailureRollsBackEntriesAndStatus()
{
    const auto created = logistics->createDraft(draft(DocumentType::Transfer, destinationId));
    QVERIFY(created.ok());
    const auto saved = documents->logisticsById(created.value());
    QVERIFY(saved.ok());
    QVERIFY(db->execute(QStringLiteral(
        "CREATE TRIGGER abort_line_snapshot BEFORE UPDATE OF batch_no_snapshot "
        "ON logistics_document_lines WHEN NEW.id=%1 "
        "BEGIN SELECT RAISE(ABORT, 'forced snapshot failure'); END")
                            .arg(saved.value().lines.front().id)));
    const auto rejected = logistics->confirm(created.value());
    QVERIFY(!rejected.ok());
    QCOMPARE(rejected.error().code, QStringLiteral("database"));
    QVERIFY(inventory->entriesForSource(QStringLiteral("Logistics"), created.value()).isEmpty());
    const auto after = documents->logisticsById(created.value());
    QVERIFY(after.ok());
    QCOMPARE(after.value().status, DocumentStatus::Draft);
    QVERIFY(after.value().lines.front().batchNoSnapshot.isEmpty());
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
    auto value = draft(DocumentType::Outbound, externalId);
    value.documentNo = QStringLiteral("L-OVERFLOW");
    const auto created = logistics->createDraft(value);
    QVERIFY(created.ok());
    const auto rejected = logistics->confirm(created.value());
    QVERIFY(!rejected.ok());
    QCOMPARE(rejected.error().code, QStringLiteral("database"));
    QVERIFY(inventory->entriesForSource(QStringLiteral("Logistics"), created.value()).isEmpty());
}

void LogisticsServiceTest::cancelQuantityFailureIsPropagated()
{
    const auto created = logistics->createDraft(draft(DocumentType::Transfer, destinationId));
    QVERIFY(created.ok());
    QVERIFY(logistics->confirm(created.value()).ok());
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
    QCOMPARE(documents->logisticsById(created.value()).value().status,
             DocumentStatus::Confirmed);
}

void LogisticsServiceTest::listIsNewestFirstAndDeleteOnlyAllowsDraft()
{
    auto older = draft(DocumentType::Transfer, destinationId);
    older.documentNo = QStringLiteral("L-OLD");
    older.businessAt = QDateTime::fromString(QStringLiteral("2026-08-01T10:00:00"), Qt::ISODate);
    auto newer = draft(DocumentType::Transfer, destinationId);
    newer.documentNo = QStringLiteral("L-NEW");
    newer.businessAt = QDateTime::fromString(QStringLiteral("2026-09-01T10:00:00"), Qt::ISODate);
    const auto oldId = logistics->createDraft(older);
    const auto newId = logistics->createDraft(newer);
    QVERIFY(oldId.ok()); QVERIFY(newId.ok());
    QVERIFY(logistics->confirm(newId.value()).ok());
    const auto listed = logistics->list(DocumentType::Transfer);
    QVERIFY(listed.ok()); QCOMPARE(listed.value().size(), 2);
    QCOMPARE(listed.value().at(0).id, newId.value());
    const auto confirmedDelete = logistics->deleteDraft(newId.value());
    QVERIFY(!confirmedDelete.ok());
    QCOMPARE(confirmedDelete.error().code, QStringLiteral("invalid_status"));
    QVERIFY(logistics->cancel(newId.value()).ok());
    QCOMPARE(logistics->deleteDraft(newId.value()).error().code,
             QStringLiteral("invalid_status"));
    QVERIFY(logistics->deleteDraft(oldId.value()).ok());
    QCOMPARE(logistics->list(DocumentType::Transfer).value().size(), 1);
}

QTEST_MAIN(LogisticsServiceTest)
#include "tst_logistics_service.moc"
