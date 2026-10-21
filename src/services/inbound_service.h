#pragma once

#include "repositories/document_repository.h"
#include "repositories/inventory_repository.h"
#include "repositories/master_data_repository.h"

class InboundService {
public:
    InboundService(Database &database, MasterDataRepository &masterData,
                   DocumentRepository &documents, InventoryRepository &inventory);

    Result<EntityId> create(const InboundInput &input);
    Result<bool> update(EntityId id, const InboundInput &input);
    Result<InboundBatch> byId(EntityId id) const;
    Result<bool> cancel(EntityId id);
    Result<QVector<InboundBatch>> list() const;

private:
    Result<EntityId> save(EntityId id, const InboundInput &input);
    Result<InboundBatch> prepare(const InboundInput &input, const InboundBatch *current) const;

    Database &database_;
    MasterDataRepository &masterData_;
    DocumentRepository &documents_;
    InventoryRepository &inventory_;
};
