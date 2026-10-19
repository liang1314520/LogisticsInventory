#pragma once

#include "core/models.h"
#include "core/result.h"
#include "db/database.h"

#include <QDateTime>
#include <QList>
#include <QVector>

struct LogisticsLineDraft {
    EntityId batchId = 0;
    qint64 quantity = 0;
    EntityId productModelId = 0;
};

struct ReturnableLine {
    EntityId batchId = 0;
    EntityId productTypeId = 0;
    EntityId productModelId = 0;
    QString batchNo;
    QString modelCode;
    qint64 quantity = 0;
    QString materialModel;
};

struct LogisticsDraft {
    QString documentNo;
    DocumentType type = DocumentType::Transfer;
    EntityId sourceLocationId = 0;
    EntityId destinationLocationId = 0;
    QDateTime businessAt;
    QString note;
    QList<LogisticsLineDraft> lines;
    EntityId outboundConfirmedById = 0;
};

struct LogisticsLine : LogisticsLineDraft {
    EntityId id = 0;
    EntityId productTypeId = 0;
    QString batchNoSnapshot;
    QString categoryNameSnapshot;
    QString typeCodeSnapshot;
    QString modelCodeSnapshot;
    QString materialModel;
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
    DocumentStatus status = DocumentStatus::Draft;
    QString sourceNameSnapshot;
    QString destinationNameSnapshot;
    QDateTime createdAt;
    QDateTime confirmedAt;
    QDateTime cancelledAt;
    EntityId outboundConfirmedById = 0;
    QString outboundConfirmedByName;
};

struct InboundDraft {
    QString batchNo;
    EntityId productTypeId = 0;
    qint64 quantity = 0;
    EntityId initialLocationId = 0;
    QDateTime businessAt;
    QString note;
    QString materialModel;
    EntityId receivedById = 0;
};

struct InboundBatch : InboundDraft {
    EntityId id = 0;
    DocumentStatus status = DocumentStatus::Draft;
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

    Result<EntityId> insertInboundDraft(const InboundDraft &draft);
    Result<bool> updateInboundDraft(EntityId id, const InboundDraft &draft);
    Result<InboundBatch> inboundById(EntityId id) const;
    Result<QVector<InboundBatch>> inboundList() const;
    Result<bool> deleteInboundDraft(EntityId id);
    Result<bool> confirmInbound(EntityId id, const QString &categoryName,
                                const QString &typeCode, const QString &locationName,
                                const QDateTime &confirmedAt, const QString &receivedByName);
    Result<bool> cancelInbound(EntityId id, const QDateTime &cancelledAt);

    Result<EntityId> insertLogisticsDraft(const LogisticsDraft &draft);
    Result<bool> updateLogisticsDraft(EntityId id, const LogisticsDraft &draft);
    Result<LogisticsDocument> logisticsById(EntityId id) const;
    Result<QVector<LogisticsDocument>> logisticsList(DocumentType type) const;
    Result<QVector<LogisticsDocument>> logisticsForBatch(EntityId batchId) const;
    Result<bool> deleteLogisticsDraft(EntityId id);
    Result<QVector<ReturnableLine>> returnableLines(EntityId factoryLocationId) const;
    Result<bool> updateLogisticsLineSnapshots(EntityId lineId, const QString &batchNo,
                                              const QString &categoryName,
                                              const QString &typeCode,
                                              const QString &modelCode);
    Result<bool> confirmLogistics(EntityId id, const QString &sourceName,
                                  const QString &destinationName,
                                  const QDateTime &confirmedAt, const QString &outboundConfirmedByName);
    Result<bool> cancelLogistics(EntityId id, const QDateTime &cancelledAt);

private:
    Result<QString> prepareNumber(const QString &requested, const QString &prefix,
                                  const QDate &businessDate, EntityId excludedId = 0);
    Database &database_;
};
