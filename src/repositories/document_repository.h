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
};

struct LogisticsDraft {
    QString documentNo;
    DocumentType type = DocumentType::Transfer;
    EntityId sourceLocationId = 0;
    EntityId destinationLocationId = 0;
    QDateTime businessAt;
    QString note;
    QList<LogisticsLineDraft> lines;
};

struct LogisticsLine : LogisticsLineDraft {
    EntityId id = 0;
    EntityId productTypeId = 0;
    QString batchNoSnapshot;
    QString categoryNameSnapshot;
    QString typeCodeSnapshot;
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
};

struct InboundDraft {
    QString batchNo;
    EntityId productTypeId = 0;
    qint64 quantity = 0;
    EntityId initialLocationId = 0;
    QDateTime businessAt;
    QString note;
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
                                const QDateTime &confirmedAt);
    Result<bool> cancelInbound(EntityId id, const QDateTime &cancelledAt);

    Result<EntityId> insertLogisticsDraft(const LogisticsDraft &draft);
    Result<bool> updateLogisticsDraft(EntityId id, const LogisticsDraft &draft);
    Result<LogisticsDocument> logisticsById(EntityId id) const;
    Result<QVector<LogisticsDocument>> logisticsList(DocumentType type) const;
    Result<bool> deleteLogisticsDraft(EntityId id);
    Result<bool> updateLogisticsLineSnapshots(EntityId lineId, const QString &batchNo,
                                              const QString &categoryName,
                                              const QString &typeCode);
    Result<bool> confirmLogistics(EntityId id, const QString &sourceName,
                                  const QString &destinationName,
                                  const QDateTime &confirmedAt);
    Result<bool> cancelLogistics(EntityId id, const QDateTime &cancelledAt);

private:
    Database &database_;
};
