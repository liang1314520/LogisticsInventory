#include "master_data_repository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

namespace {
template<class T> Result<T> failure(const QSqlQuery &query, const QString &field = {})
{
    return Result<T>::failure({QStringLiteral("database"), query.lastError().text(), field});
}
template<class T> Result<T> missing(const QString &field = QStringLiteral("id"))
{
    return Result<T>::failure({QStringLiteral("not_found"), QStringLiteral("记录不存在"), field});
}
QString locationTypeText(LocationType type)
{
    switch (type) {
    case LocationType::Company: return QStringLiteral("Company");
    case LocationType::Factory: return QStringLiteral("Factory");
    case LocationType::Customer: return QStringLiteral("Customer");
    case LocationType::ProjectSite: return QStringLiteral("ProjectSite");
    case LocationType::Other: return QStringLiteral("Other");
    }
    return QStringLiteral("Other");
}
LocationType locationType(const QString &text)
{
    if (text == QStringLiteral("Company")) return LocationType::Company;
    if (text == QStringLiteral("Factory")) return LocationType::Factory;
    if (text == QStringLiteral("Customer")) return LocationType::Customer;
    if (text == QStringLiteral("ProjectSite")) return LocationType::ProjectSite;
    return LocationType::Other;
}
ProductCategory category(const QSqlQuery &q)
{
    return {q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toBool()};
}
ProductType productType(const QSqlQuery &q)
{
    return {q.value(0).toLongLong(), q.value(1).toLongLong(), q.value(2).toString(),
            q.value(3).toString(), q.value(4).toBool()};
}
Location location(const QSqlQuery &q)
{
    return {q.value(0).toLongLong(), q.value(1).toString(),
            locationType(q.value(2).toString()), q.value(3).toBool(), q.value(4).toBool()};
}

template<class Item>
Result<QVector<Item>> executeList(Database &database, const QString &sql,
                                  Item (*reader)(const QSqlQuery &))
{
    QSqlQuery query(database.connection());
    if (!query.exec(sql))
        return failure<QVector<Item>>(query);
    QVector<Item> rows;
    while (query.next())
        rows.append(reader(query));
    if (query.lastError().isValid())
        return failure<QVector<Item>>(query);
    return Result<QVector<Item>>::success(rows);
}

static Result<bool> exists(Database &database, const QString &table, const QString &column,
                           const QString &value, EntityId excludeId)
{
    QSqlQuery query(database.connection());
    query.prepare(QStringLiteral("SELECT 1 FROM %1 WHERE %2=? AND (?=0 OR id<>?)")
                      .arg(table, column));
    query.addBindValue(value);
    query.addBindValue(excludeId);
    query.addBindValue(excludeId);
    if (!query.exec())
        return failure<bool>(query);
    return Result<bool>::success(query.next());
}

// 按 SQL 中 '?' 的个数重复绑定同一个 id，避免 ?N 编号占位符在 QSQLITE 下的解析差异。
bool runWithRepeatedId(QSqlQuery &query, const QString &sql, EntityId id)
{
    query.prepare(sql);
    const int placeholders = sql.count(QLatin1Char('?'));
    for (int index = 0; index < placeholders; ++index)
        query.addBindValue(id);
    return query.exec();
}

Result<qint64> countScalar(Database &database, const QString &sql, EntityId id)
{
    QSqlQuery query(database.connection());
    if (!runWithRepeatedId(query, sql, id) || !query.next())
        return failure<qint64>(query);
    return Result<qint64>::success(query.value(0).toLongLong());
}

// 先按依赖顺序清理引用方，最后删除目标行；整体在一个事务内，任一步失败全部回滚。
// inventory_entries.reversal_of_entry_id 是自引用外键，同一条 DELETE 可能同时命中
// 冲销记录和被冲销记录，因此在事务内延迟外键检查到提交时统一校验。
Result<bool> cascade(Database &database, const QStringList &statements,
                     const QString &finalStatement, EntityId id)
{
    ServiceError operationError;
    QString transactionError;
    const bool committed = database.transaction(Database::TransactionMode::Immediate, [&] {
        QSqlQuery defer(database.connection());
        if (!defer.exec(QStringLiteral("PRAGMA defer_foreign_keys = ON"))) {
            operationError = {QStringLiteral("database"), defer.lastError().text(), {}};
            return false;
        }
        for (const QString &sql : statements) {
            QSqlQuery query(database.connection());
            if (!runWithRepeatedId(query, sql, id)) {
                operationError = {QStringLiteral("database"), query.lastError().text(), {}};
                return false;
            }
        }
        QSqlQuery target(database.connection());
        if (!runWithRepeatedId(target, finalStatement, id)) {
            operationError = {QStringLiteral("database"), target.lastError().text(), {}};
            return false;
        }
        if (!target.numRowsAffected()) {
            operationError = {QStringLiteral("not_found"), QStringLiteral("记录不存在"),
                              QStringLiteral("id")};
            return false;
        }
        return true;
    }, &transactionError);
    if (!committed) {
        if (operationError.code.isEmpty())
            operationError = {QStringLiteral("database"), transactionError, {}};
        return Result<bool>::failure(operationError);
    }
    return Result<bool>::success(true);
}
}

MasterDataRepository::MasterDataRepository(Database &database) : database_(database) {}

Result<ProductCategory> MasterDataRepository::insertCategory(const QString &name)
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral("INSERT INTO product_categories(name) VALUES(?)"));
    query.addBindValue(name);
    if (!query.exec()) return failure<ProductCategory>(query, QStringLiteral("name"));
    return categoryById(query.lastInsertId().toLongLong());
}

Result<ProductType> MasterDataRepository::insertProductType(
    EntityId categoryId, const QString &typeCode, const QString &description)
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "INSERT INTO product_types(category_id,type_code,description) VALUES(?,?,?)"));
    query.addBindValue(categoryId);
    query.addBindValue(typeCode);
    query.addBindValue(description.isNull() ? QStringLiteral("") : description);
    if (!query.exec()) return failure<ProductType>(query, QStringLiteral("typeCode"));
    return productTypeById(query.lastInsertId().toLongLong());
}

Result<Location> MasterDataRepository::insertLocation(const QString &name, LocationType type,
                                                      bool countsAsInventory)
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "INSERT INTO locations(name,type,counts_as_inventory) VALUES(?,?,?)"));
    query.addBindValue(name);
    query.addBindValue(locationTypeText(type));
    query.addBindValue(countsAsInventory);
    if (!query.exec()) return failure<Location>(query);
    return locationById(query.lastInsertId().toLongLong());
}

Result<ProductCategory> MasterDataRepository::updateCategory(EntityId id, const QString &name)
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral("UPDATE product_categories SET name=? WHERE id=?"));
    query.addBindValue(name); query.addBindValue(id);
    if (!query.exec()) return failure<ProductCategory>(query, QStringLiteral("name"));
    if (!query.numRowsAffected()) return missing<ProductCategory>();
    return categoryById(id);
}

Result<ProductType> MasterDataRepository::updateProductType(
    EntityId id, EntityId categoryId, const QString &typeCode, const QString &description)
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "UPDATE product_types SET category_id=?,type_code=?,description=? WHERE id=?"));
    query.addBindValue(categoryId); query.addBindValue(typeCode);
    query.addBindValue(description.isNull() ? QStringLiteral("") : description);
    query.addBindValue(id);
    if (!query.exec()) return failure<ProductType>(query, QStringLiteral("typeCode"));
    if (!query.numRowsAffected()) return missing<ProductType>();
    return productTypeById(id);
}

Result<Location> MasterDataRepository::updateLocation(
    EntityId id, const QString &name, LocationType type, bool countsAsInventory)
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "UPDATE locations SET name=?,type=?,counts_as_inventory=? WHERE id=?"));
    query.addBindValue(name); query.addBindValue(locationTypeText(type));
    query.addBindValue(countsAsInventory); query.addBindValue(id);
    if (!query.exec()) return failure<Location>(query);
    if (!query.numRowsAffected()) return missing<Location>();
    return locationById(id);
}

Result<ProductCategory> MasterDataRepository::categoryById(EntityId id) const
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral("SELECT id,name,enabled FROM product_categories WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec()) return failure<ProductCategory>(query);
    if (!query.next()) return missing<ProductCategory>();
    return Result<ProductCategory>::success(category(query));
}

Result<ProductType> MasterDataRepository::productTypeById(EntityId id) const
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "SELECT id,category_id,type_code,description,enabled FROM product_types WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec()) return failure<ProductType>(query);
    if (!query.next()) return missing<ProductType>();
    return Result<ProductType>::success(productType(query));
}

Result<Location> MasterDataRepository::locationById(EntityId id) const
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "SELECT id,name,type,counts_as_inventory,enabled FROM locations WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec()) return failure<Location>(query);
    if (!query.next()) return missing<Location>();
    return Result<Location>::success(location(query));
}

Result<QVector<ProductCategory>> MasterDataRepository::enabledCategories() const
{
    return executeList<ProductCategory>(
        database_, QStringLiteral(
            "SELECT id,name,enabled FROM product_categories WHERE enabled=1 ORDER BY name"),
        category);
}

Result<QVector<ProductType>> MasterDataRepository::enabledProductTypes() const
{
    QSqlQuery query(database_.connection());
    if (!query.exec(QStringLiteral(
            "SELECT t.id,t.category_id,t.type_code,t.description,t.enabled "
            "FROM product_types t JOIN product_categories c ON c.id=t.category_id "
            "WHERE t.enabled=1 AND c.enabled=1 ORDER BY c.name,t.type_code,t.id")))
        return failure<QVector<ProductType>>(query);
    QVector<ProductType> rows;
    while (query.next()) rows.append(productType(query));
    if (query.lastError().isValid()) return failure<QVector<ProductType>>(query);
    return Result<QVector<ProductType>>::success(rows);
}

Result<QVector<Location>> MasterDataRepository::enabledLocations() const
{
    QSqlQuery query(database_.connection());
    if (!query.exec(QStringLiteral(
            "SELECT id,name,type,counts_as_inventory,enabled FROM locations "
            "WHERE enabled=1 ORDER BY name")))
        return failure<QVector<Location>>(query);
    QVector<Location> rows;
    while (query.next()) rows.append(location(query));
    if (query.lastError().isValid()) return failure<QVector<Location>>(query);
    return Result<QVector<Location>>::success(rows);
}

Result<QVector<ProductCategory>> MasterDataRepository::allCategories() const
{
    return executeList<ProductCategory>(
        database_, QStringLiteral(
            "SELECT id,name,enabled FROM product_categories ORDER BY name,id"), category);
}

Result<QVector<ProductType>> MasterDataRepository::allProductTypes() const
{
    QSqlQuery query(database_.connection());
    if (!query.exec(QStringLiteral(
            "SELECT id,category_id,type_code,description,enabled FROM product_types "
            "ORDER BY category_id,type_code,id")))
        return failure<QVector<ProductType>>(query);
    QVector<ProductType> rows;
    while (query.next()) rows.append(productType(query));
    if (query.lastError().isValid()) return failure<QVector<ProductType>>(query);
    return Result<QVector<ProductType>>::success(rows);
}

Result<QVector<Location>> MasterDataRepository::allLocations() const
{
    QSqlQuery query(database_.connection());
    if (!query.exec(QStringLiteral(
            "SELECT id,name,type,counts_as_inventory,enabled FROM locations ORDER BY name,id")))
        return failure<QVector<Location>>(query);
    QVector<Location> rows;
    while (query.next()) rows.append(location(query));
    if (query.lastError().isValid()) return failure<QVector<Location>>(query);
    return Result<QVector<Location>>::success(rows);
}

Result<bool> MasterDataRepository::categoryNameExists(
    const QString &value, EntityId excludeId) const
{
    return exists(database_, QStringLiteral("product_categories"), QStringLiteral("name"),
                  value, excludeId);
}

Result<bool> MasterDataRepository::productTypeCodeExists(
    EntityId categoryId, const QString &typeCode, EntityId excludeId) const
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "SELECT 1 FROM product_types WHERE category_id=? AND type_code=? "
        "AND (?=0 OR id<>?)"));
    query.addBindValue(categoryId); query.addBindValue(typeCode);
    query.addBindValue(excludeId); query.addBindValue(excludeId);
    if (!query.exec()) return failure<bool>(query);
    return Result<bool>::success(query.next());
}

Result<bool> MasterDataRepository::locationNameExists(
    const QString &value, EntityId excludeId) const
{
    return exists(database_, QStringLiteral("locations"), QStringLiteral("name"), value,
                  excludeId);
}

Result<bool> MasterDataRepository::setEnabled(Entity entity, EntityId id, bool enabled)
{
    const QStringList tables = {QStringLiteral("product_categories"),
                                QStringLiteral("product_types"),
                                QStringLiteral("locations")};
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral("UPDATE %1 SET enabled=? WHERE id=?")
                      .arg(tables.at(static_cast<int>(entity))));
    query.addBindValue(enabled); query.addBindValue(id);
    if (!query.exec()) return failure<bool>(query);
    if (!query.numRowsAffected()) return missing<bool>();
    return Result<bool>::success(true);
}

Result<qint64> MasterDataRepository::countCategoryReferences(EntityId id) const
{
    return countScalar(database_, QStringLiteral(
        "SELECT COUNT(*) FROM inbound_batches b JOIN product_types t ON t.id=b.product_type_id "
        "WHERE t.category_id=?"), id);
}

Result<qint64> MasterDataRepository::countProductTypeReferences(EntityId id) const
{
    return countScalar(database_, QStringLiteral(
        "SELECT COUNT(*) FROM inbound_batches WHERE product_type_id=?"), id);
}

// 只统计用户可见的业务单据（入库批次 + 物流单）。库存流水必然挂在某张单据下，
// 计入会让提示数字虚高。
Result<qint64> MasterDataRepository::countLocationReferences(EntityId id) const
{
    return countScalar(database_, QStringLiteral(
        "SELECT (SELECT COUNT(*) FROM inbound_batches WHERE initial_location_id=?)"
        "+(SELECT COUNT(*) FROM logistics_documents "
        "WHERE source_location_id=? OR destination_location_id=?)"), id);
}

Result<bool> MasterDataRepository::deleteCategoryCascade(EntityId id)
{
    // 受影响单据的全部流水都要清掉，否则会留下指向已删单据的孤儿流水。
    const QStringList statements{
        QStringLiteral("DELETE FROM inventory_entries WHERE source_type='Logistics' "
                       "AND source_id IN (SELECT line.document_id "
                       "FROM logistics_document_lines line "
                       "JOIN inbound_batches b ON b.id=line.batch_id "
                       "JOIN product_types t ON t.id=b.product_type_id WHERE t.category_id=?)"),
        QStringLiteral("DELETE FROM logistics_documents WHERE id IN "
                       "(SELECT line.document_id FROM logistics_document_lines line "
                       "JOIN inbound_batches b ON b.id=line.batch_id "
                       "JOIN product_types t ON t.id=b.product_type_id WHERE t.category_id=?)"),
        QStringLiteral("DELETE FROM inventory_entries WHERE batch_id IN "
                       "(SELECT b.id FROM inbound_batches b JOIN product_types t "
                       "ON t.id=b.product_type_id WHERE t.category_id=?)"),
        QStringLiteral("DELETE FROM inbound_batches WHERE product_type_id IN "
                       "(SELECT id FROM product_types WHERE category_id=?)"),
        QStringLiteral("DELETE FROM product_types WHERE category_id=?")
    };
    return cascade(database_, statements,
                   QStringLiteral("DELETE FROM product_categories WHERE id=?"), id);
}

Result<bool> MasterDataRepository::deleteProductTypeCascade(EntityId id)
{
    const QStringList statements{
        QStringLiteral("DELETE FROM inventory_entries WHERE source_type='Logistics' "
                       "AND source_id IN (SELECT line.document_id "
                       "FROM logistics_document_lines line "
                       "JOIN inbound_batches b ON b.id=line.batch_id "
                       "WHERE b.product_type_id=?)"),
        QStringLiteral("DELETE FROM logistics_documents WHERE id IN "
                       "(SELECT line.document_id FROM logistics_document_lines line "
                       "JOIN inbound_batches b ON b.id=line.batch_id "
                       "WHERE b.product_type_id=?)"),
        QStringLiteral("DELETE FROM inventory_entries WHERE batch_id IN "
                       "(SELECT id FROM inbound_batches WHERE product_type_id=?)"),
        QStringLiteral("DELETE FROM inbound_batches WHERE product_type_id=?")
    };
    return cascade(database_, statements,
                   QStringLiteral("DELETE FROM product_types WHERE id=?"), id);
}

Result<bool> MasterDataRepository::deleteLocationCascade(EntityId id)
{
    // 库位被三处引用：入库初始库位、物流单来源/目的地、库存流水所在库位。
    // 顺序必须是「先清流水 → 再删单据 → 最后删批次」，否则外键会拦住中间步骤。
    const QStringList statements{
        QStringLiteral("DELETE FROM inventory_entries WHERE source_type='Logistics' "
                       "AND source_id IN (SELECT id FROM logistics_documents "
                       "WHERE source_location_id=? OR destination_location_id=?)"),
        QStringLiteral("DELETE FROM inventory_entries WHERE source_type='Logistics' "
                       "AND source_id IN (SELECT line.document_id "
                       "FROM logistics_document_lines line "
                       "JOIN inbound_batches b ON b.id=line.batch_id "
                       "WHERE b.initial_location_id=?)"),
        QStringLiteral("DELETE FROM logistics_documents "
                       "WHERE source_location_id=? OR destination_location_id=?"),
        QStringLiteral("DELETE FROM logistics_documents WHERE id IN "
                       "(SELECT line.document_id FROM logistics_document_lines line "
                       "JOIN inbound_batches b ON b.id=line.batch_id "
                       "WHERE b.initial_location_id=?)"),
        QStringLiteral("DELETE FROM inventory_entries WHERE batch_id IN "
                       "(SELECT id FROM inbound_batches WHERE initial_location_id=?)"),
        QStringLiteral("DELETE FROM inventory_entries WHERE location_id=?"),
        QStringLiteral("DELETE FROM inbound_batches WHERE initial_location_id=?")
    };
    return cascade(database_, statements,
                   QStringLiteral("DELETE FROM locations WHERE id=?"), id);
}
