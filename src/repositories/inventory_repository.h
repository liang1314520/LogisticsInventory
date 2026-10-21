#pragma once

#include "core/models.h"
#include "core/result.h"
#include "db/database.h"

#include <QDateTime>
#include <QVector>

struct InventoryEntry {
    EntityId id = 0;
    EntityId batchId = 0;
    EntityId locationId = 0;
    QString sourceType;
    EntityId sourceId = 0;
    EntityId sourceLineId = 0;
    qint64 quantityDelta = 0;
    QDateTime businessAt;
    QDateTime createdAt;
    EntityId reversalOfEntryId = 0;
};

class InventoryRepository {
public:
    explicit InventoryRepository(Database &database);

    qint64 quantity(EntityId batchId, EntityId locationId) const;
    Result<qint64> quantityResult(EntityId batchId, EntityId locationId) const;
    QVector<InventoryEntry> entriesForSource(const QString &sourceType,
                                             EntityId sourceId) const;
    Result<QVector<InventoryEntry>> entriesForSourceResult(
        const QString &sourceType, EntityId sourceId) const;
    Result<InventoryEntry> insertEntry(EntityId batchId, EntityId locationId,
                                       const QString &sourceType, EntityId sourceId,
                                       EntityId sourceLineId, qint64 quantityDelta,
                                       const QDateTime &businessAt,
                                       EntityId reversalOfEntryId = 0);
    Result<QVector<InventoryEntry>> unreversedEntriesForSource(
        const QString &sourceType, EntityId sourceId) const;
    Result<bool> removeEntriesForSource(const QString &sourceType, EntityId sourceId);

private:
    Database &database_;
};
