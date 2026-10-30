#pragma once

#include "core/models.h"
#include "core/result.h"
#include "db/database.h"

#include <QDateTime>
#include <QList>
#include <QVector>

#include <optional>

struct LogisticsLineInput {
    EntityId batchId = 0;
    qint64 quantity = 0;
    EntityId productModelId = 0;
    std::optional<PackagingQuantities> packaging;
};

struct LogisticsInput {
    QString documentNo;
    DocumentType type = DocumentType::Transfer;
    EntityId sourceLocationId = 0;
    EntityId destinationLocationId = 0;
    QDateTime businessAt;
    QString note;
    QList<LogisticsLineInput> lines;
    EntityId outboundConfirmedById = 0;
};

struct LogisticsLine : LogisticsLineInput {
    EntityId id = 0;
    EntityId productTypeId = 0;
    QString batchNoSnapshot;
    QString categoryNameSnapshot;
    QString typeCodeSnapshot;
    QString modelCodeSnapshot;
    QString materialModel;
    PackagingSpec packagingSpec;
};

struct LogisticsDocument {
    EntityId id = 0;
    QString documentNo;
    DocumentType type = DocumentType::Transfer;
    EntityId sourceLocationId = 0;
    EntityId destinationLocationId = 0;
    QDateTime businessAt;
    QString note;
    QList<LogisticsLine> lines;
    DocumentStatus status = DocumentStatus::Confirmed;
    QString sourceNameSnapshot;
    QString destinationNameSnapshot;
    QDateTime createdAt;
    QDateTime confirmedAt;
    QDateTime cancelledAt;
    EntityId outboundConfirmedById = 0;
    QString outboundConfirmedByName;
};

struct InboundInput {
    QString batchNo;
    EntityId productTypeId = 0;
    qint64 quantity = 0;
    EntityId initialLocationId = 0;
    QDateTime businessAt;
    QString note;
    QString materialModel;
    EntityId receivedById = 0;
};

struct InboundBatch : InboundInput {
    EntityId id = 0;
    DocumentStatus status = DocumentStatus::Confirmed;
    QString categoryNameSnapshot;
    QString typeCodeSnapshot;
    QString locationNameSnapshot;
    QDateTime createdAt;
    QDateTime confirmedAt;
    QDateTime cancelledAt;
    QString receivedByName;
};

class DocumentRepository {
public:
    explicit DocumentRepository(Database &database);

    Result<EntityId> insertInbound(const InboundBatch &batch);
    Result<bool> updateInbound(const InboundBatch &batch);
    Result<InboundBatch> inboundById(EntityId id) const;
    Result<QVector<InboundBatch>> inboundList() const;
    Result<bool> updateBatchSnapshots(EntityId batchId);
    Result<bool> cancelInbound(EntityId id, const QDateTime &cancelledAt);

    Result<EntityId> insertLogistics(const LogisticsDocument &document);
    Result<bool> updateLogistics(const LogisticsDocument &document);
    Result<LogisticsDocument> logisticsById(EntityId id) const;
    Result<QVector<LogisticsDocument>> logisticsList(DocumentType type) const;
    Result<QVector<LogisticsDocument>> logisticsForBatch(EntityId batchId) const;
    Result<bool> cancelLogistics(EntityId id, const QDateTime &cancelledAt);

private:
    Result<bool> insertLogisticsLines(EntityId documentId, const QList<LogisticsLine> &lines);
    Result<QString> prepareNumber(const QString &requested, const QString &prefix,
                                  const QDate &businessDate, EntityId excludedId = 0);
    Database &database_;
};
