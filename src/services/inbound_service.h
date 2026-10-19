#pragma once

#include "repositories/document_repository.h"
#include "repositories/inventory_repository.h"
#include "repositories/master_data_repository.h"

class InboundService {
public:
    InboundService(Database &database, MasterDataRepository &masterData,
                   DocumentRepository &documents, InventoryRepository &inventory);

    Result<EntityId> createDraft(const InboundDraft &draft);
    Result<bool> updateDraft(EntityId id, const InboundDraft &draft);
    Result<InboundBatch> byId(EntityId id) const;
    Result<bool> confirm(EntityId id);
    Result<bool> cancel(EntityId id);
    Result<QVector<InboundBatch>> list() const;
    Result<bool> deleteDraft(EntityId id);
    Result<EntityId> copyToDraft(EntityId id);

private:
    ServiceError validateDraft(const InboundDraft &draft, bool requireNumber = true) const;

    Database &database_;
    MasterDataRepository &masterData_;
    DocumentRepository &documents_;
    InventoryRepository &inventory_;
};
