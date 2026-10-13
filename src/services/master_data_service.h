#pragma once

#include "repositories/master_data_repository.h"

class MasterDataService {
public:
    explicit MasterDataService(MasterDataRepository &repository);

    Result<ProductCategory> createCategory(const QString &name);
    Result<ProductType> createProductType(EntityId categoryId, const QString &typeCode,
                                          const QString &description);
    Result<Location> createLocation(const QString &name, LocationType type,
                                    bool countsAsInventory);
    Result<ProductCategory> updateCategory(EntityId id, const QString &name);
    Result<ProductType> updateProductType(EntityId id, EntityId categoryId,
                                          const QString &typeCode,
                                          const QString &description);
    Result<Location> updateLocation(EntityId id, const QString &name, LocationType type,
                                    bool countsAsInventory);

    Result<ProductCategory> categoryById(EntityId id) const;
    Result<ProductType> productTypeById(EntityId id) const;
    Result<Location> locationById(EntityId id) const;

    Result<QVector<ProductCategory>> enabledCategoriesResult() const;
    Result<QVector<ProductType>> enabledProductTypesResult() const;
    Result<QVector<Location>> enabledLocationsResult() const;
    Result<QVector<ProductCategory>> allCategoriesResult() const;
    Result<QVector<ProductType>> allProductTypesResult() const;
    Result<QVector<Location>> allLocationsResult() const;
    QVector<ProductCategory> enabledCategories() const;
    QVector<ProductType> enabledProductTypes() const;
    QVector<Location> enabledLocations() const;

    Result<bool> setCategoryEnabled(EntityId id, bool enabled);
    Result<bool> setProductTypeEnabled(EntityId id, bool enabled);
    Result<bool> setLocationEnabled(EntityId id, bool enabled);

    Result<qint64> categoryReferenceCount(EntityId id) const;
    Result<qint64> productTypeReferenceCount(EntityId id) const;
    Result<qint64> locationReferenceCount(EntityId id) const;
    Result<bool> deleteCategory(EntityId id);
    Result<bool> deleteProductType(EntityId id);
    Result<bool> deleteLocation(EntityId id);

private:
    MasterDataRepository &repository_;
};
