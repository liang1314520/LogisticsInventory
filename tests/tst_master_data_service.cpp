#include "repositories/document_repository.h"
#include "repositories/inventory_repository.h"
#include "services/inbound_service.h"
#include "services/logistics_service.h"
#include "services/master_data_service.h"
#include "test_support.h"

#include <algorithm>
#include <memory>
#include <QtTest>

class MasterDataServiceTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void disabledDataIsExcludedFromNewDocuments();
    void validatesRequiredFieldsAndScopedBusinessKeys();
    void validatesUpdatesAndScopedBusinessKeys();
    void rejectsMissingAssociations();
    void listsAllProductTypes();
    void createsLocationsWithExplicitInventoryFlag();
    void deletesUnreferencedMasterData();
    void reportsReferenceCountsForUsedMasterData();
    void deleteCascadeRemovesDependentDocumentsAndEntries();
    void deleteRejectsMissingRecords();
    void productModelsSupportCrudScopedKeysAndEnablement();
    void deletingModelCascadesDocumentsAndReturns();

private:
    std::unique_ptr<TestDatabase> db;
    std::unique_ptr<MasterDataRepository> repository;
    std::unique_ptr<MasterDataService> service;
};

void MasterDataServiceTest::init()
{
    db = std::make_unique<TestDatabase>();
    QVERIFY(db->openAndMigrate());
    repository = std::make_unique<MasterDataRepository>(db->database());
    service = std::make_unique<MasterDataService>(*repository);
}

void MasterDataServiceTest::disabledDataIsExcludedFromNewDocuments()
{
    const auto enabledCategory = service->createCategory(QStringLiteral("设备"));
    const auto disabledCategory = service->createCategory(QStringLiteral("停用分类"));
    QVERIFY(enabledCategory.ok() && disabledCategory.ok());
    const auto disabledType = service->createProductType(enabledCategory.value().id,
                                                         QStringLiteral("GW-01"), QString());
    const auto hiddenByCategory = service->createProductType(disabledCategory.value().id,
                                                              QStringLiteral("OLD-01"), QString());
    QVERIFY(disabledType.ok() && hiddenByCategory.ok());
    QVERIFY(service->setProductTypeEnabled(disabledType.value().id, false).ok());
    QVERIFY(service->setCategoryEnabled(disabledCategory.value().id, false).ok());

    const auto enabled = service->enabledProductTypesResult();
    QVERIFY(enabled.ok());
    QVERIFY(std::none_of(enabled.value().cbegin(), enabled.value().cend(),
                         [&](const ProductType &item) {
                             return item.id == disabledType.value().id ||
                                    item.id == hiddenByCategory.value().id;
                         }));
    const auto historical = service->productTypeById(disabledType.value().id);
    QVERIFY(historical.ok());
    QCOMPARE(historical.value().typeCode, QStringLiteral("GW-01"));
}

void MasterDataServiceTest::validatesRequiredFieldsAndScopedBusinessKeys()
{
    const auto blankCategory = service->createCategory(QStringLiteral("  "));
    QVERIFY(!blankCategory.ok());
    QCOMPARE(blankCategory.error().field, QStringLiteral("name"));

    const auto safe = service->createCategory(QStringLiteral("保险箱"));
    const auto gateway = service->createCategory(QStringLiteral("网关"));
    QVERIFY(safe.ok() && gateway.ok());
    const auto trimmed = service->createProductType(safe.value().id,
                                                    QStringLiteral("  F25  "),
                                                    QStringLiteral("  标准型  "));
    QVERIFY(trimmed.ok());
    QCOMPARE(trimmed.value().typeCode, QStringLiteral("F25"));
    QCOMPARE(trimmed.value().description, QStringLiteral("标准型"));
    const auto duplicateType = service->createProductType(safe.value().id,
                                                           QStringLiteral("F25"), {});
    QVERIFY(!duplicateType.ok());
    QCOMPARE(duplicateType.error().field, QStringLiteral("typeCode"));
    QVERIFY(service->createProductType(gateway.value().id, QStringLiteral("F25"), {}).ok());

    const auto blankCode = service->createProductType(safe.value().id, QStringLiteral(" "), {});
    QVERIFY(!blankCode.ok());
    QCOMPARE(blankCode.error().field, QStringLiteral("typeCode"));
}

void MasterDataServiceTest::validatesUpdatesAndScopedBusinessKeys()
{
    const auto firstCategory = service->createCategory(QStringLiteral("设备"));
    const auto secondCategory = service->createCategory(QStringLiteral("耗材"));
    QVERIFY(firstCategory.ok() && secondCategory.ok());
    const auto duplicateCategory = service->updateCategory(secondCategory.value().id,
                                                           firstCategory.value().name);
    QVERIFY(!duplicateCategory.ok());
    QCOMPARE(duplicateCategory.error().code, QStringLiteral("duplicate"));
    QCOMPARE(duplicateCategory.error().field, QStringLiteral("name"));

    const auto firstType = service->createProductType(firstCategory.value().id,
                                                      QStringLiteral("GW-01"), QString());
    const auto secondType = service->createProductType(firstCategory.value().id,
                                                       QStringLiteral("SW-01"), QString());
    QVERIFY(firstType.ok() && secondType.ok());
    const auto duplicateType = service->updateProductType(secondType.value().id,
        firstCategory.value().id, firstType.value().typeCode, QString());
    QVERIFY(!duplicateType.ok());
    QCOMPARE(duplicateType.error().code, QStringLiteral("duplicate"));
    QCOMPARE(duplicateType.error().field, QStringLiteral("typeCode"));
    QVERIFY(service->updateProductType(secondType.value().id, secondCategory.value().id,
                                       QStringLiteral("  GW-01  "), QStringLiteral("  新说明  ")).ok());

    const auto missingCategory = service->updateProductType(secondType.value().id, 999,
        secondType.value().typeCode, QString());
    QVERIFY(!missingCategory.ok());
    QCOMPARE(missingCategory.error().field, QStringLiteral("categoryId"));

    const auto missingEntity = service->updateProductType(999, firstCategory.value().id,
                                                           QStringLiteral("X"), QString());
    QVERIFY(!missingEntity.ok());
    QCOMPARE(missingEntity.error().code, QStringLiteral("not_found"));
    QCOMPARE(missingEntity.error().field, QStringLiteral("id"));
}

void MasterDataServiceTest::rejectsMissingAssociations()
{
    const auto type = service->createProductType(999, QStringLiteral("GW-01"), QString());
    QVERIFY(!type.ok());
    QCOMPARE(type.error().code, QStringLiteral("not_found"));
    QCOMPARE(type.error().field, QStringLiteral("categoryId"));
}

void MasterDataServiceTest::listsAllProductTypes()
{
    const auto category = service->createCategory(QStringLiteral("设备"));
    QVERIFY(category.ok());
    const auto type = service->createProductType(category.value().id,
                                                 QStringLiteral("GW-01"), QString());
    QVERIFY(type.ok());
    QVERIFY(service->setProductTypeEnabled(type.value().id, false).ok());
    const auto all = service->allProductTypesResult();
    QVERIFY(all.ok());
    QCOMPARE(all.value().size(), 1);
    QCOMPARE(all.value().front().id, type.value().id);
}

void MasterDataServiceTest::createsLocationsWithExplicitInventoryFlag()
{
    const auto customer = service->createLocation(QStringLiteral("客户现场"), LocationType::Customer, false);
    QVERIFY(customer.ok());
    QVERIFY(!customer.value().countsAsInventory);
    QVERIFY(service->setLocationEnabled(customer.value().id, false).ok());

    const auto enabled = service->enabledLocations();
    QVERIFY(enabled.isEmpty());
    const auto historical = service->locationById(customer.value().id);
    QVERIFY(historical.ok());
    QCOMPARE(historical.value().type, LocationType::Customer);
}

void MasterDataServiceTest::deletesUnreferencedMasterData()
{
    const auto category = service->createCategory(QStringLiteral("设备"));
    QVERIFY(category.ok());
    const auto type = service->createProductType(category.value().id,
                                                 QStringLiteral("GW-01"), QString());
    const auto location = service->createLocation(QStringLiteral("公司仓"),
                                                  LocationType::Company, true);
    QVERIFY(type.ok() && location.ok());

    const auto typeRefs = service->productTypeReferenceCount(type.value().id);
    QVERIFY(typeRefs.ok());
    QCOMPARE(typeRefs.value(), qint64(0));

    QVERIFY(service->deleteProductType(type.value().id).ok());
    QVERIFY(service->deleteLocation(location.value().id).ok());
    QVERIFY(service->deleteCategory(category.value().id).ok());

    QVERIFY(service->allProductTypesResult().value().isEmpty());
    QVERIFY(service->allLocationsResult().value().isEmpty());
    QVERIFY(service->allCategoriesResult().value().isEmpty());
}

void MasterDataServiceTest::reportsReferenceCountsForUsedMasterData()
{
    const auto category = service->createCategory(QStringLiteral("设备"));
    QVERIFY(category.ok());
    const auto type = service->createProductType(category.value().id,
                                                 QStringLiteral("GW-01"), QString());
    const auto location = service->createLocation(QStringLiteral("公司仓"),
                                                  LocationType::Company, true);
    QVERIFY(type.ok() && location.ok());

    DocumentRepository documents(db->database());
    const auto batch = documents.insertInboundDraft({QStringLiteral("B-1"), type.value().id,
        100, location.value().id, QDateTime(QDate(2026, 9, 1), QTime(8, 0)), {}});
    QVERIFY(batch.ok());

    const auto typeRefs = service->productTypeReferenceCount(type.value().id);
    QVERIFY(typeRefs.ok());
    QCOMPARE(typeRefs.value(), qint64(1));

    const auto categoryRefs = service->categoryReferenceCount(category.value().id);
    QVERIFY(categoryRefs.ok());
    QCOMPARE(categoryRefs.value(), qint64(1));

    const auto locationRefs = service->locationReferenceCount(location.value().id);
    QVERIFY(locationRefs.ok());
    QCOMPARE(locationRefs.value(), qint64(1));
}

void MasterDataServiceTest::deleteCascadeRemovesDependentDocumentsAndEntries()
{
    const auto category = service->createCategory(QStringLiteral("设备"));
    QVERIFY(category.ok());
    const auto type = service->createProductType(category.value().id,
                                                 QStringLiteral("GW-01"), QString());
    const auto other = service->createProductType(category.value().id,
                                                  QStringLiteral("ACC-01"), QString());
    const auto source = service->createLocation(QStringLiteral("公司仓"),
                                                LocationType::Company, true);
    const auto destination = service->createLocation(QStringLiteral("工厂A"),
                                                     LocationType::Factory, true);
    QVERIFY(type.ok() && other.ok() && source.ok() && destination.ok());

    DocumentRepository documents(db->database());
    InventoryRepository inventory(db->database());
    InboundService inbound(db->database(), *repository, documents, inventory);
    LogisticsService logistics(db->database(), *repository, documents, inventory);

    const auto target = inbound.createDraft({QStringLiteral("B-TARGET"), type.value().id, 100,
        source.value().id, QDateTime(QDate(2026, 9, 1), QTime(8, 0)), {}});
    const auto keep = inbound.createDraft({QStringLiteral("B-KEEP"), other.value().id, 50,
        source.value().id, QDateTime(QDate(2026, 9, 1), QTime(8, 0)), {}});
    QVERIFY(target.ok() && keep.ok());
    QVERIFY(inbound.confirm(target.value()).ok());
    QVERIFY(inbound.confirm(keep.value()).ok());

    // 同一张调拨单混合两个批次：一个待删，一个保留。
    const auto mixed = logistics.createDraft({QStringLiteral("T-MIX"), DocumentType::Transfer,
        source.value().id, destination.value().id, QDateTime(QDate(2026, 9, 2), QTime(9, 0)), {},
        {{target.value(), 40}, {keep.value(), 10}}});
    QVERIFY(mixed.ok());
    QVERIFY(logistics.confirm(mixed.value()).ok());

    QVERIFY(service->deleteProductType(type.value().id).ok());

    // 待删类型、其批次、以及引用它的整张单据都应消失。
    QVERIFY(!documents.inboundById(target.value()).ok());
    QVERIFY(!documents.logisticsById(mixed.value()).ok());
    const auto remainingTypes = service->allProductTypesResult();
    QVERIFY(remainingTypes.ok());
    QCOMPARE(remainingTypes.value().size(), 1);
    QCOMPARE(remainingTypes.value().front().id, other.value().id);

    // 保留批次本身还在，且不留下指向已删单据的孤儿流水。
    QVERIFY(documents.inboundById(keep.value()).ok());
    const auto orphans = inventory.entriesForSourceResult(QStringLiteral("Logistics"),
                                                          mixed.value());
    QVERIFY(orphans.ok());
    QVERIFY(orphans.value().isEmpty());
    QVERIFY(db->scalar(QStringLiteral("PRAGMA foreign_key_check")).isNull());
}

void MasterDataServiceTest::deleteRejectsMissingRecords()
{
    const auto category = service->deleteCategory(999);
    QVERIFY(!category.ok());
    QCOMPARE(category.error().code, QStringLiteral("not_found"));

    const auto type = service->deleteProductType(999);
    QVERIFY(!type.ok());
    QCOMPARE(type.error().code, QStringLiteral("not_found"));

    const auto location = service->deleteLocation(999);
    QVERIFY(!location.ok());
    QCOMPARE(location.error().code, QStringLiteral("not_found"));
}

void MasterDataServiceTest::productModelsSupportCrudScopedKeysAndEnablement()
{
    const auto category = service->createCategory(QStringLiteral("设备"));
    QVERIFY(category.ok());
    const auto safe = service->createProductType(category.value().id, QStringLiteral("保险箱"), {});
    const auto gateway = service->createProductType(category.value().id, QStringLiteral("网关"), {});
    QVERIFY(safe.ok() && gateway.ok());
    const auto model = service->createProductModel(safe.value().id, QStringLiteral(" F25 "), {});
    QVERIFY(model.ok());
    QCOMPARE(model.value().modelCode, QStringLiteral("F25"));
    QCOMPARE(service->createProductModel(safe.value().id, QStringLiteral("F25"), {}).error().code,
             QStringLiteral("duplicate"));
    QCOMPARE(service->createProductModel(safe.value().id, QStringLiteral(" "), {}).error().field,
             QStringLiteral("modelCode"));
    QVERIFY(service->createProductModel(gateway.value().id, QStringLiteral("F25"), {}).ok());
    QCOMPARE(service->createProductModel(999, QStringLiteral("F25"), {}).error().field,
             QStringLiteral("productTypeId"));
    QVERIFY(service->updateProductModel(model.value().id, safe.value().id, QStringLiteral("P25"), {}).ok());
    QCOMPARE(service->productModelById(model.value().id).value().modelCode, QStringLiteral("P25"));
    QCOMPARE(service->enabledProductModelsForTypeResult(safe.value().id).value().size(), 1);
    QVERIFY(service->setProductModelEnabled(model.value().id, false).ok());
    QVERIFY(service->enabledProductModelsForTypeResult(safe.value().id).value().isEmpty());
    QCOMPARE(service->allProductModelsResult().value().size(), 2);
    QVERIFY(service->setProductModelEnabled(model.value().id, true).ok());
    QVERIFY(service->setProductTypeEnabled(safe.value().id, false).ok());
    QVERIFY(service->enabledProductModelsForTypeResult(safe.value().id).value().isEmpty());
    QVERIFY(service->deleteProductType(safe.value().id).ok());
    QVERIFY(!service->productModelById(model.value().id).ok());
    const auto remaining = service->allProductModelsResult();
    QVERIFY(remaining.ok());
    QCOMPARE(remaining.value().size(), 1);
    QVERIFY(service->deleteProductModel(remaining.value().front().id).ok());
    QVERIFY(service->allProductModelsResult().value().isEmpty());
    QCOMPARE(db->scalar(QStringLiteral("SELECT COUNT(*) FROM pragma_foreign_key_check")).toInt(), 0);
}

void MasterDataServiceTest::deletingModelCascadesDocumentsAndReturns()
{
    const auto category = service->createCategory(QStringLiteral("设备"));
    QVERIFY(category.ok());
    const auto type = service->createProductType(category.value().id, QStringLiteral("保险箱"), {});
    QVERIFY(type.ok());
    const auto f25 = service->createProductModel(type.value().id, QStringLiteral("F25"), {});
    const auto p25 = service->createProductModel(type.value().id, QStringLiteral("P25"), {});
    const auto warehouse = service->createLocation(QStringLiteral("仓库"), LocationType::Company, true);
    const auto factory = service->createLocation(QStringLiteral("外部工厂"), LocationType::Factory, false);
    QVERIFY(f25.ok() && p25.ok() && warehouse.ok() && factory.ok());
    DocumentRepository documents(db->database());
    InventoryRepository inventory(db->database());
    InboundService inbound(db->database(), *repository, documents, inventory);
    LogisticsService logistics(db->database(), *repository, documents, inventory);
    const auto at = QDateTime::currentDateTime();
    const auto batch = inbound.createDraft({QStringLiteral("B-1"), type.value().id, 100,
                                            warehouse.value().id, at, {}});
    QVERIFY(batch.ok());
    QVERIFY(inbound.confirm(batch.value()).ok());
    const auto sent = logistics.createDraft({QStringLiteral("O-MIX"), DocumentType::Outbound,
        warehouse.value().id, factory.value().id, at, {},
        {{batch.value(), 50, f25.value().id}, {batch.value(), 50, p25.value().id}}});
    QVERIFY(sent.ok());
    QVERIFY(logistics.confirm(sent.value()).ok());
    const auto returned = logistics.createDraft({QStringLiteral("R-1"), DocumentType::Return,
        factory.value().id, warehouse.value().id, at, {}, {{batch.value(), 5, f25.value().id}}});
    QVERIFY(returned.ok());
    QVERIFY(logistics.confirm(returned.value()).ok());
    QVERIFY(logistics.cancel(returned.value()).ok());
    QVERIFY(service->deleteProductModel(f25.value().id).ok());
    QVERIFY(!documents.logisticsById(sent.value()).ok());
    QVERIFY(!documents.logisticsById(returned.value()).ok());
    QVERIFY(service->productModelById(p25.value().id).ok());
    QCOMPARE(inventory.quantity(batch.value(), warehouse.value().id), qint64(100));
    QCOMPARE(db->scalar(QStringLiteral("SELECT COUNT(*) FROM inventory_entries WHERE source_type='Logistics'")).toInt(), 0);
    QCOMPARE(db->scalar(QStringLiteral("SELECT COUNT(*) FROM pragma_foreign_key_check")).toInt(), 0);
}

QTEST_MAIN(MasterDataServiceTest)
#include "tst_master_data_service.moc"
