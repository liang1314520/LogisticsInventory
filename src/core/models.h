#pragma once

#include <QMetaType>
#include <QString>

using EntityId = qint64;

enum class DocumentStatus { Draft, Confirmed, Cancelled };
enum class DocumentType { Transfer, Outbound };
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

struct Location {
    EntityId id = 0;
    QString name;
    LocationType type = LocationType::Other;
    bool countsAsInventory = false;
    bool enabled = true;
};

Q_DECLARE_METATYPE(DocumentStatus)
Q_DECLARE_METATYPE(DocumentType)
Q_DECLARE_METATYPE(LocationType)
