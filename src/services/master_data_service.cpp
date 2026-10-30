#include "master_data_service.h"

#include "core/packaging.h"

namespace {
ServiceError required(const QString &field)
{
    return {QStringLiteral("required"), QStringLiteral("必填字段不能为空"), field};
}
ServiceError duplicate(const QString &field)
{
    return {QStringLiteral("duplicate"), QStringLiteral("业务键已存在"), field};
}
template<class T> Result<T> forwardError(const ServiceError &error)
{
    return Result<T>::failure(error);
}
template<class T> Result<T> categoryError(const ServiceError &error)
{
    if (error.code == QStringLiteral("database"))
        return forwardError<T>(error);
    return Result<T>::failure({QStringLiteral("not_found"), QStringLiteral("分类不存在"),
                               QStringLiteral("categoryId")});
}
template<class T> Result<T> productTypeError(const ServiceError &error)
{
    if (error.code == QStringLiteral("database"))
        return forwardError<T>(error);
    return Result<T>::failure({QStringLiteral("not_found"), QStringLiteral("产品类型不存在"),
                               QStringLiteral("productTypeId")});
}
}

MasterDataService::MasterDataService(MasterDataRepository &repository) : repository_(repository) {}

Result<ProductCategory> MasterDataService::createCategory(const QString &name)
{
    const QString value = name.trimmed();
    if (value.isEmpty()) return Result<ProductCategory>::failure(required(QStringLiteral("name")));
    const auto exists = repository_.categoryNameExists(value);
    if (!exists.ok()) return forwardError<ProductCategory>(exists.error());
    if (exists.value()) return Result<ProductCategory>::failure(duplicate(QStringLiteral("name")));
    return repository_.insertCategory(value);
}

Result<ProductType> MasterDataService::createProductType(
    EntityId categoryId, const QString &typeCode, const QString &description)
{
    const auto category = repository_.categoryById(categoryId);
    if (!category.ok()) return categoryError<ProductType>(category.error());
    const QString code = typeCode.trimmed();
    if (code.isEmpty()) return Result<ProductType>::failure(required(QStringLiteral("typeCode")));
    const auto exists = repository_.productTypeCodeExists(categoryId, code);
    if (!exists.ok()) return forwardError<ProductType>(exists.error());
    if (exists.value()) return Result<ProductType>::failure(duplicate(QStringLiteral("typeCode")));
    return repository_.insertProductType(categoryId, code, description.trimmed());
}

Result<ProductModel> MasterDataService::createProductModel(
    EntityId productTypeId, const QString &modelCode, const QString &description,
    const PackagingSpec &packaging)
{
    const auto valid = validatePackagingSpec(packaging);
    if (!valid.ok()) return forwardError<ProductModel>(valid.error());
    const auto type = repository_.productTypeById(productTypeId);
    if (!type.ok()) return productTypeError<ProductModel>(type.error());
    const QString code = modelCode.trimmed();
    if (code.isEmpty()) return Result<ProductModel>::failure(required(QStringLiteral("modelCode")));
    const auto exists = repository_.productModelCodeExists(productTypeId, code);
    if (!exists.ok()) return forwardError<ProductModel>(exists.error());
    if (exists.value()) return Result<ProductModel>::failure(duplicate(QStringLiteral("modelCode")));
    return repository_.insertProductModel(productTypeId, code, description.trimmed(), packaging);
}

Result<Location> MasterDataService::createLocation(const QString &name, LocationType type,
                                                   bool countsAsInventory)
{
    const QString value = name.trimmed();
    if (value.isEmpty()) return Result<Location>::failure(required(QStringLiteral("name")));
    if ((type == LocationType::Company || type == LocationType::Factory) && !countsAsInventory)
        return Result<Location>::failure({QStringLiteral("invalid"),
            type == LocationType::Factory
                ? QStringLiteral("工厂必须计入库存，公司发往工厂请使用调拨单")
                : QStringLiteral("公司库位必须计入库存"),
            QStringLiteral("countsAsInventory")});
    const auto exists = repository_.locationNameExists(value);
    if (!exists.ok()) return forwardError<Location>(exists.error());
    if (exists.value()) return Result<Location>::failure(duplicate(QStringLiteral("name")));
    return repository_.insertLocation(value, type, countsAsInventory);
}

Result<ProductCategory> MasterDataService::updateCategory(EntityId id, const QString &name)
{
    const auto current = repository_.categoryById(id);
    if (!current.ok()) return forwardError<ProductCategory>(current.error());
    const QString value = name.trimmed();
    if (value.isEmpty()) return Result<ProductCategory>::failure(required(QStringLiteral("name")));
    const auto exists = repository_.categoryNameExists(value, id);
    if (!exists.ok()) return forwardError<ProductCategory>(exists.error());
    if (exists.value()) return Result<ProductCategory>::failure(duplicate(QStringLiteral("name")));
    return repository_.updateCategory(id, value);
}

Result<ProductType> MasterDataService::updateProductType(
    EntityId id, EntityId categoryId, const QString &typeCode, const QString &description)
{
    const auto current = repository_.productTypeById(id);
    if (!current.ok()) return forwardError<ProductType>(current.error());
    const auto category = repository_.categoryById(categoryId);
    if (!category.ok()) return categoryError<ProductType>(category.error());
    const QString code = typeCode.trimmed();
    if (code.isEmpty()) return Result<ProductType>::failure(required(QStringLiteral("typeCode")));
    const auto exists = repository_.productTypeCodeExists(categoryId, code, id);
    if (!exists.ok()) return forwardError<ProductType>(exists.error());
    if (exists.value()) return Result<ProductType>::failure(duplicate(QStringLiteral("typeCode")));
    return repository_.updateProductType(id, categoryId, code, description.trimmed());
}

Result<ProductModel> MasterDataService::updateProductModel(
    EntityId id, EntityId productTypeId, const QString &modelCode, const QString &description,
    const PackagingSpec &packaging)
{
    const auto valid = validatePackagingSpec(packaging);
    if (!valid.ok()) return forwardError<ProductModel>(valid.error());
    const auto current = repository_.productModelById(id);
    if (!current.ok()) return forwardError<ProductModel>(current.error());
    const auto type = repository_.productTypeById(productTypeId);
    if (!type.ok()) return productTypeError<ProductModel>(type.error());
    const QString code = modelCode.trimmed();
    if (code.isEmpty()) return Result<ProductModel>::failure(required(QStringLiteral("modelCode")));
    const auto exists = repository_.productModelCodeExists(productTypeId, code, id);
    if (!exists.ok()) return forwardError<ProductModel>(exists.error());
    if (exists.value()) return Result<ProductModel>::failure(duplicate(QStringLiteral("modelCode")));
    return repository_.updateProductModel(id, productTypeId, code, description.trimmed(), packaging);
}

Result<Location> MasterDataService::updateLocation(
    EntityId id, const QString &name, LocationType type, bool countsAsInventory)
{
    const auto current = repository_.locationById(id);
    if (!current.ok()) return forwardError<Location>(current.error());
    const QString value = name.trimmed();
    if (value.isEmpty()) return Result<Location>::failure(required(QStringLiteral("name")));
    if ((type == LocationType::Company || type == LocationType::Factory) && !countsAsInventory)
        return Result<Location>::failure({QStringLiteral("invalid"),
            type == LocationType::Factory
                ? QStringLiteral("工厂必须计入库存，公司发往工厂请使用调拨单")
                : QStringLiteral("公司库位必须计入库存"),
            QStringLiteral("countsAsInventory")});
    const auto exists = repository_.locationNameExists(value, id);
    if (!exists.ok()) return forwardError<Location>(exists.error());
    if (exists.value()) return Result<Location>::failure(duplicate(QStringLiteral("name")));
    return repository_.updateLocation(id, value, type, countsAsInventory);
}

Result<ProductCategory> MasterDataService::categoryById(EntityId id) const
{
    return repository_.categoryById(id);
}
Result<ProductType> MasterDataService::productTypeById(EntityId id) const
{
    return repository_.productTypeById(id);
}
Result<ProductModel> MasterDataService::productModelById(EntityId id) const
{
    return repository_.productModelById(id);
}
Result<Location> MasterDataService::locationById(EntityId id) const
{
    return repository_.locationById(id);
}
Result<QVector<ProductCategory>> MasterDataService::enabledCategoriesResult() const
{
    return repository_.enabledCategories();
}
Result<QVector<ProductType>> MasterDataService::enabledProductTypesResult() const
{
    return repository_.enabledProductTypes();
}
Result<QVector<ProductModel>> MasterDataService::enabledProductModelsResult() const
{
    return repository_.enabledProductModels();
}
Result<QVector<ProductModel>> MasterDataService::enabledProductModelsForTypeResult(
    EntityId productTypeId) const
{
    return repository_.enabledProductModelsForType(productTypeId);
}
Result<QVector<Location>> MasterDataService::enabledLocationsResult() const
{
    return repository_.enabledLocations();
}
Result<QVector<ProductCategory>> MasterDataService::allCategoriesResult() const
{
    return repository_.allCategories();
}
Result<QVector<ProductType>> MasterDataService::allProductTypesResult() const
{
    return repository_.allProductTypes();
}
Result<QVector<ProductModel>> MasterDataService::allProductModelsResult() const
{
    return repository_.allProductModels();
}
Result<QVector<Location>> MasterDataService::allLocationsResult() const
{
    return repository_.allLocations();
}
QVector<ProductCategory> MasterDataService::enabledCategories() const
{
    const auto result = enabledCategoriesResult();
    return result.ok() ? result.value() : QVector<ProductCategory>{};
}
QVector<ProductType> MasterDataService::enabledProductTypes() const
{
    const auto result = enabledProductTypesResult();
    return result.ok() ? result.value() : QVector<ProductType>{};
}
QVector<Location> MasterDataService::enabledLocations() const
{
    const auto result = enabledLocationsResult();
    return result.ok() ? result.value() : QVector<Location>{};
}
Result<bool> MasterDataService::setCategoryEnabled(EntityId id, bool enabled)
{
    return repository_.setEnabled(MasterDataRepository::Entity::Category, id, enabled);
}
Result<bool> MasterDataService::setProductTypeEnabled(EntityId id, bool enabled)
{
    return repository_.setEnabled(MasterDataRepository::Entity::ProductType, id, enabled);
}
Result<bool> MasterDataService::setProductModelEnabled(EntityId id, bool enabled)
{
    return repository_.setEnabled(MasterDataRepository::Entity::ProductModel, id, enabled);
}
Result<bool> MasterDataService::setLocationEnabled(EntityId id, bool enabled)
{
    return repository_.setEnabled(MasterDataRepository::Entity::Location, id, enabled);
}
Result<Personnel> MasterDataService::createPersonnel(const QString &name)
{
    const QString value = name.trimmed();
    if (value.isEmpty()) return Result<Personnel>::failure(required(QStringLiteral("name")));
    const auto exists = repository_.personnelNameExists(value);
    if (!exists.ok()) return forwardError<Personnel>(exists.error());
    if (exists.value())
        return Result<Personnel>::failure({QStringLiteral("duplicate"),
            QStringLiteral("人员姓名已存在，同名人员可添加部门等标识区分"), QStringLiteral("name")});
    return repository_.insertPersonnel(value);
}
Result<Personnel> MasterDataService::updatePersonnel(EntityId id, const QString &name)
{
    const auto current = repository_.personnelById(id);
    if (!current.ok()) return forwardError<Personnel>(current.error());
    const QString value = name.trimmed();
    if (value.isEmpty()) return Result<Personnel>::failure(required(QStringLiteral("name")));
    const auto exists = repository_.personnelNameExists(value, id);
    if (!exists.ok()) return forwardError<Personnel>(exists.error());
    if (exists.value())
        return Result<Personnel>::failure({QStringLiteral("duplicate"),
            QStringLiteral("人员姓名已存在，同名人员可添加部门等标识区分"), QStringLiteral("name")});
    return repository_.updatePersonnel(id, value);
}
Result<Personnel> MasterDataService::personnelById(EntityId id) const
{
    return repository_.personnelById(id);
}
Result<QVector<Personnel>> MasterDataService::enabledPersonnelResult() const
{
    return repository_.enabledPersonnel();
}
Result<QVector<Personnel>> MasterDataService::allPersonnelResult() const
{
    return repository_.allPersonnel();
}
Result<bool> MasterDataService::setPersonnelEnabled(EntityId id, bool enabled)
{
    return repository_.setEnabled(MasterDataRepository::Entity::Personnel, id, enabled);
}
Result<qint64> MasterDataService::categoryReferenceCount(EntityId id) const
{
    return repository_.countCategoryReferences(id);
}
Result<qint64> MasterDataService::productTypeReferenceCount(EntityId id) const
{
    return repository_.countProductTypeReferences(id);
}
Result<qint64> MasterDataService::productModelReferenceCount(EntityId id) const
{
    return repository_.countProductModelReferences(id);
}
Result<qint64> MasterDataService::locationReferenceCount(EntityId id) const
{
    return repository_.countLocationReferences(id);
}
Result<bool> MasterDataService::deleteCategory(EntityId id)
{
    return repository_.deleteCategoryCascade(id);
}
Result<bool> MasterDataService::deleteProductType(EntityId id)
{
    return repository_.deleteProductTypeCascade(id);
}
Result<bool> MasterDataService::deleteProductModel(EntityId id)
{
    return repository_.deleteProductModelCascade(id);
}
Result<bool> MasterDataService::deleteLocation(EntityId id)
{
    return repository_.deleteLocationCascade(id);
}
