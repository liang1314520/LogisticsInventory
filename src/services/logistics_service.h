#pragma once

#include "repositories/document_repository.h"
#include "repositories/inventory_repository.h"
#include "repositories/master_data_repository.h"

class LogisticsService {
public:
    LogisticsService(Database &database, MasterDataRepository &masterData,
                     DocumentRepository &documents, InventoryRepository &inventory);

    Result<EntityId> createDraft(const LogisticsDraft &draft);
    Result<bool> updateDraft(EntityId id, const LogisticsDraft &draft);
    Result<LogisticsDocument> byId(EntityId id) const;
    Result<bool> confirm(EntityId id);
    Result<bool> cancel(EntityId id);
    Result<EntityId> copyToDraft(EntityId id);
    Result<QVector<LogisticsDocument>> list(DocumentType type) const;
    Result<bool> deleteDraft(EntityId id);

private:
    ServiceError validateDraft(const LogisticsDraft &draft, bool requireNumber = true) const;
    ServiceError validateDocument(const LogisticsDocument &document) const;

    Database &database_;
    MasterDataRepository &masterData_;
    DocumentRepository &documents_;
    InventoryRepository &inventory_;
};
