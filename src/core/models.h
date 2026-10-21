#pragma once

#include <QMetaType>
#include <QString>

using EntityId = qint64;

enum class DocumentStatus { Confirmed, Cancelled };
// Return：工厂退回公司，双方均计库存，总库存不变。
enum class DocumentType { Transfer, Outbound, Return };
enum class LocationType { Company, Factory, Customer, ProjectSite, Other };

struct ProductCategory {
    EntityId id = 0;
    QString name;
    bool enabled = true;
};
using Category = ProductCategory;

struct ProductType {
    EntityId id = 0;
    EntityId categoryId = 0;
    QString typeCode;
    QString description;
    bool enabled = true;
};

// 调拨/出库型号（如 F25/P25）与入库批次的物料型号（如 V40）独立。
// 库存仍按批次和库位汇总。
struct ProductModel {
    EntityId id = 0;
    EntityId productTypeId = 0;
    QString modelCode;
    QString description;
    bool enabled = true;
};

struct Location {
    EntityId id = 0;
    QString name;
    LocationType type = LocationType::Other;
    bool countsAsInventory = false;
    bool enabled = true;
};

struct Personnel {
    EntityId id = 0;
    QString name;
    bool enabled = true;
};

Q_DECLARE_METATYPE(DocumentStatus)
Q_DECLARE_METATYPE(DocumentType)
Q_DECLARE_METATYPE(LocationType)
