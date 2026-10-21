#include "inventory_repository.h"

#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#include <limits>

namespace {
InventoryEntry readEntry(const QSqlQuery &query)
{
    InventoryEntry entry;
    entry.id = query.value(0).toLongLong();
    entry.batchId = query.value(1).toLongLong();
    entry.locationId = query.value(2).toLongLong();
    entry.sourceType = query.value(3).toString();
    entry.sourceId = query.value(4).toLongLong();
    entry.sourceLineId = query.value(5).toLongLong();
    entry.quantityDelta = query.value(6).toLongLong();
    entry.businessAt = QDateTime::fromString(query.value(7).toString(), Qt::ISODate);
    entry.createdAt = QDateTime::fromString(query.value(8).toString(), Qt::ISODate);
    entry.reversalOfEntryId = query.value(9).toLongLong();
    return entry;
}

ServiceError databaseError(const QSqlQuery &query)
{
    return {QStringLiteral("database"), query.lastError().text(), {}};
}

QString selectColumns(const QString &prefix = {})
{
    const QStringList names = {
        QStringLiteral("id"), QStringLiteral("batch_id"), QStringLiteral("location_id"),
        QStringLiteral("source_type"), QStringLiteral("source_id"),
        QStringLiteral("source_line_id"), QStringLiteral("quantity_delta"),
        QStringLiteral("business_at"), QStringLiteral("created_at"),
        QStringLiteral("reversal_of_entry_id")};
    if (prefix.isEmpty())
        return names.join(QLatin1Char(','));
    QStringList qualified;
    for (const QString &name : names)
        qualified.append(prefix + QLatin1Char('.') + name);
    return qualified.join(QLatin1Char(','));
}
}

InventoryRepository::InventoryRepository(Database &database) : database_(database) {}

qint64 InventoryRepository::quantity(EntityId batchId, EntityId locationId) const
{
    const auto result = quantityResult(batchId, locationId);
    return result.ok() ? result.value() : 0;
}

Result<qint64> InventoryRepository::quantityResult(EntityId batchId,
                                                    EntityId locationId) const
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(quantity_delta),0) FROM inventory_entries "
        "WHERE batch_id=? AND location_id=?"));
    query.addBindValue(batchId);
    query.addBindValue(locationId);
    if (!query.exec() || !query.next())
        return Result<qint64>::failure(databaseError(query));
    const QVariant value = query.value(0);
    bool valid = false;
    const qint64 quantity = value.toLongLong(&valid);
    if (!valid) {
        return Result<qint64>::failure(
            {QStringLiteral("database"), QStringLiteral("库存数量读取失败"), {}});
    }
    return Result<qint64>::success(quantity);
}

QVector<InventoryEntry> InventoryRepository::entriesForSource(
    const QString &sourceType, EntityId sourceId) const
{
    const auto result = entriesForSourceResult(sourceType, sourceId);
    return result.ok() ? result.value() : QVector<InventoryEntry>{};
}

Result<QVector<InventoryEntry>> InventoryRepository::entriesForSourceResult(
    const QString &sourceType, EntityId sourceId) const
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral("SELECT %1 FROM inventory_entries "
                                 "WHERE source_type=? AND source_id=? ORDER BY id")
                      .arg(selectColumns()));
    query.addBindValue(sourceType);
    query.addBindValue(sourceId);
    if (!query.exec())
        return Result<QVector<InventoryEntry>>::failure(databaseError(query));
    QVector<InventoryEntry> entries;
    while (query.next())
        entries.append(readEntry(query));
    if (query.lastError().isValid())
        return Result<QVector<InventoryEntry>>::failure(databaseError(query));
    return Result<QVector<InventoryEntry>>::success(entries);
}

Result<InventoryEntry> InventoryRepository::insertEntry(
    EntityId batchId, EntityId locationId, const QString &sourceType,
    EntityId sourceId, EntityId sourceLineId, qint64 quantityDelta,
    const QDateTime &businessAt, EntityId reversalOfEntryId)
{
    const auto current = quantityResult(batchId, locationId);
    if (!current.ok())
        return Result<InventoryEntry>::failure(current.error());
    if ((quantityDelta > 0 && current.value() > std::numeric_limits<qint64>::max() - quantityDelta) ||
        (quantityDelta < 0 && current.value() < std::numeric_limits<qint64>::min() - quantityDelta)) {
        return Result<InventoryEntry>::failure({QStringLiteral("quantity_overflow"),
            QStringLiteral("库存数量超出范围"), QStringLiteral("quantity")});
    }
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "INSERT INTO inventory_entries(batch_id,location_id,source_type,source_id,"
        "source_line_id,quantity_delta,business_at,created_at,reversal_of_entry_id) "
        "VALUES(?,?,?,?,?,?,?,?,?)"));
    query.addBindValue(batchId);
    query.addBindValue(locationId);
    query.addBindValue(sourceType);
    query.addBindValue(sourceId);
    query.addBindValue(sourceLineId ? QVariant(sourceLineId) : QVariant());
    query.addBindValue(quantityDelta);
    query.addBindValue(businessAt.toString(Qt::ISODate));
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    query.addBindValue(reversalOfEntryId ? QVariant(reversalOfEntryId) : QVariant());
    if (!query.exec())
        return Result<InventoryEntry>::failure(databaseError(query));

    QSqlQuery saved(database_.connection());
    saved.prepare(QStringLiteral("SELECT %1 FROM inventory_entries WHERE id=?")
                      .arg(selectColumns()));
    saved.addBindValue(query.lastInsertId());
    if (!saved.exec() || !saved.next())
        return Result<InventoryEntry>::failure(databaseError(saved));
    return Result<InventoryEntry>::success(readEntry(saved));
}

Result<QVector<InventoryEntry>> InventoryRepository::unreversedEntriesForSource(
    const QString &sourceType, EntityId sourceId) const
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral(
        "SELECT %1 FROM inventory_entries original "
        "WHERE original.source_type=? AND original.source_id=? "
        "AND original.reversal_of_entry_id IS NULL "
        "AND NOT EXISTS(SELECT 1 FROM inventory_entries reversal "
        "WHERE reversal.reversal_of_entry_id=original.id) ORDER BY original.id")
                      .arg(selectColumns(QStringLiteral("original"))));
    query.addBindValue(sourceType);
    query.addBindValue(sourceId);
    if (!query.exec())
        return Result<QVector<InventoryEntry>>::failure(databaseError(query));
    QVector<InventoryEntry> entries;
    while (query.next())
        entries.append(readEntry(query));
    if (query.lastError().isValid())
        return Result<QVector<InventoryEntry>>::failure(databaseError(query));
    return Result<QVector<InventoryEntry>>::success(entries);
}

Result<bool> InventoryRepository::removeEntriesForSource(const QString &sourceType, EntityId sourceId)
{
    const auto entries = entriesForSourceResult(sourceType, sourceId);
    if (!entries.ok())
        return Result<bool>::failure(entries.error());
    // 先移除冲销行，再删除被引用的原始流水。
    for (const bool reversals : {true, false}) {
        QSqlQuery query(database_.connection());
        query.prepare(QStringLiteral(
            "DELETE FROM inventory_entries WHERE source_type=? AND source_id=? "
            "AND reversal_of_entry_id IS %1NULL").arg(reversals ? QStringLiteral("NOT ") : QString()));
        query.addBindValue(sourceType);
        query.addBindValue(sourceId);
        if (!query.exec())
            return Result<bool>::failure(databaseError(query));
    }
    QSet<QPair<EntityId, EntityId>> checked;
    for (const auto &entry : entries.value()) {
        const auto key = qMakePair(entry.batchId, entry.locationId);
        if (checked.contains(key))
            continue;
        checked.insert(key);
        const auto balance = quantityResult(entry.batchId, entry.locationId);
        if (!balance.ok())
            return Result<bool>::failure(balance.error());
    }
    return Result<bool>::success(true);
}
