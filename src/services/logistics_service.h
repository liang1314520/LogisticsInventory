#pragma once

#include "repositories/document_repository.h"
#include "repositories/inventory_repository.h"
#include "repositories/master_data_repository.h"

class LogisticsService {
public:
    LogisticsService(Database &database, MasterDataRepository &masterData,
                     DocumentRepository &documents, InventoryRepository &inventory);

    Result<EntityId> create(const LogisticsInput &input);
    Result<bool> update(EntityId id, const LogisticsInput &input);
    Result<LogisticsDocument> byId(EntityId id) const;
    Result<bool> cancel(EntityId id);
    Result<QVector<LogisticsDocument>> list(DocumentType type) const;

private:
    Result<EntityId> save(EntityId id, const LogisticsInput &input);
    Result<LogisticsDocument> prepare(const LogisticsInput &input,
                                      const LogisticsDocument *current) const;
    Result<bool> writeEntries(const LogisticsDocument &document);

    Database &database_;
    MasterDataRepository &masterData_;
    DocumentRepository &documents_;
    InventoryRepository &inventory_;
};
