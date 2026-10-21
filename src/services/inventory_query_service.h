#pragma once

#include "core/models.h"
#include "core/result.h"
#include "db/database.h"

#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QVector>

struct BatchBalance {
    EntityId batchId = 0;
    QString batchNo;
    QString categoryName;
    QString typeCode;
    qint64 quantity = 0;
    QString materialModel;
};

struct AvailableBatch {
    EntityId batchId = 0;
    EntityId productTypeId = 0;
    QString batchNo;
    QString categoryName;
    QString typeCode;
    qint64 quantity = 0;
    QString materialModel;
};

struct LocationBalance {
    EntityId locationId = 0;
    QString locationName;
    qint64 quantity = 0;
};

struct BatchLocationBalance {
    EntityId locationId = 0;
    QString locationName;
    qint64 quantity = 0;
};

struct NegativeBalance {
    EntityId batchId = 0;
    EntityId locationId = 0;
    QString batchNo;
    QString locationName;
    qint64 quantity = 0;
};

struct BatchTimelineItem {
    EntityId id = 0;
    QString sourceDocument;
    QString direction;
    qint64 quantityDelta = 0;
    QString locationName;
    QDateTime businessAt;
    QDateTime createdAt;
    QString batchNo;
    QString categoryName;
    QString typeCode;
    QString modelCode;
    DocumentStatus documentStatus = DocumentStatus::Confirmed;
    bool isReversal = false;
    QString materialModel;
};

struct DateRange {
    QDate from;
    QDate to;
    QTime fromTime{0, 0};
    QTime toTime{23, 59};
};

struct ProductTypeShipment {
    QString categoryName;
    QString typeCode;
    qint64 quantity = 0;
    QString modelCode;
    qint64 returnedQuantity = 0; // 工厂退回公司数量，属于内部流转，不抵扣对外出库。
    QString materialModel;
};

struct BusinessRecord {
    QString sourceDocument;
    QString businessType;
    QDateTime businessAt;
    QString sourceName;
    QString destinationName;
    qint64 quantity = 0;
};

struct DashboardSummary {
    qint64 totalInventory = 0;
    qint64 companyInventory = 0;
    qint64 factoryInventory = 0;
    qint64 monthlyInbound = 0;
    qint64 monthlyOutbound = 0;
    qint64 monthlyReturned = 0;
    qint64 negativeBalanceCount = 0;
    QVector<BusinessRecord> recentRecords;
};

class InventoryQueryService {
public:
    explicit InventoryQueryService(Database &database);
    Result<qint64> balance(EntityId batchId, EntityId locationId) const;
    Result<qint64> batchRemaining(EntityId batchId) const;
    Result<QVector<AvailableBatch>> availableBatches(EntityId locationId) const;
    Result<QVector<BatchBalance>> batchBalances() const;
    Result<QVector<BatchBalance>> historicalBatches() const;
    Result<QVector<LocationBalance>> locationBalances() const;
    Result<qint64> negativeBalanceCount() const;
    Result<QVector<NegativeBalance>> negativeBalances() const;
    Result<QVector<BatchLocationBalance>> batchLocationBalances(EntityId batchId) const;
    Result<QVector<BatchTimelineItem>> batchTimeline(EntityId batchId) const;
    Result<DashboardSummary> dashboardSummary(const QDate &month) const;
    Result<QVector<ProductTypeShipment>> productTypeShipmentSummary(const DateRange &range) const;

private:
    Database &database_;
};
