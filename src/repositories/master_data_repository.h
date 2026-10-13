#pragma once

#include "core/models.h"
#include "core/result.h"
#include "db/database.h"

#include <QVector>

class MasterDataRepository {
public:
    enum class Entity { Category, ProductType, Location };

    explicit MasterDataRepository(Database &database);

    Result<ProductCategory> insertCategory(const QString &name);
    Result<ProductType> insertProductType(EntityId categoryId, const QString &typeCode,
                                          const QString &description);
    Result<Location> insertLocation(const QString &name, LocationType type,
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

    Result<QVector<ProductCategory>> enabledCategories() const;
    Result<QVector<ProductType>> enabledProductTypes() const;
    Result<QVector<Location>> enabledLocations() const;
    Result<QVector<ProductCategory>> allCategories() const;
    Result<QVector<ProductType>> allProductTypes() const;
    Result<QVector<Location>> allLocations() const;

    Result<bool> categoryNameExists(const QString &name, EntityId excludeId = 0) const;
    Result<bool> productTypeCodeExists(EntityId categoryId, const QString &typeCode,
                                       EntityId excludeId = 0) const;
    Result<bool> locationNameExists(const QString &name, EntityId excludeId = 0) const;
    Result<bool> setEnabled(Entity entity, EntityId id, bool enabled);

    Result<qint64> countCategoryReferences(EntityId id) const;
    Result<qint64> countProductTypeReferences(EntityId id) const;
    Result<qint64> countLocationReferences(EntityId id) const;
    Result<bool> deleteCategoryCascade(EntityId id);
    Result<bool> deleteProductTypeCascade(EntityId id);
    Result<bool> deleteLocationCascade(EntityId id);

private:
    Database &database_;
};
