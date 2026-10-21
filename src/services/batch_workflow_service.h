#pragma once

#include "core/models.h"
#include "core/result.h"
#include "services/inventory_query_service.h"

#include <QStringList>
#include <QVector>

class DocumentRepository;
class InventoryRepository;
class MasterDataService;

enum class WorkflowStepStatus {
    NotStarted,
    InProgress,
    Completed,
    NotApplicable,
    NeedsAction
};

enum class WorkflowAction {
    GoToMasterData,
    EditInbound,
    CreateTransfer,
    CreateOutbound,
    CreateReturn,
    ViewTracking
};

struct WorkflowBatchOption {
    EntityId batchId = 0;
    QString batchNo;
    QString productTypeName;
    qint64 currentQuantity = 0;
    DocumentStatus status = DocumentStatus::Confirmed;
    QString materialModel;
};

struct WorkflowStep {
    int number = 0;
    QString title;
    WorkflowStepStatus status = WorkflowStepStatus::NotStarted;
    QString summary;
    QString prerequisiteMessage;
    int targetPage = -1;
    QString actionText;
};

struct BatchWorkflowState {
    EntityId batchId = 0;
    QString batchNo;
    QString productTypeName;
    qint64 currentQuantity = 0;
    QVector<BatchLocationBalance> currentLocations;
    int currentStep = 0;
    QString overallStatus;
    QVector<WorkflowStep> steps;
    QVector<WorkflowAction> availableActions;
    QStringList blockingReasons;
    QString nextActionText;
    int nextPage = -1;
};

class BatchWorkflowService {
public:
    BatchWorkflowService(MasterDataService &masterData, DocumentRepository &documents,
                         InventoryRepository &inventory, InventoryQueryService &queries);

    Result<BatchWorkflowState> state(EntityId batchId) const;
    Result<QVector<WorkflowBatchOption>> batchOptions() const;
    Result<bool> canStartAction(EntityId batchId, WorkflowAction action) const;

private:
    MasterDataService &masterData_;
    DocumentRepository &documents_;
    InventoryRepository &inventory_;
    InventoryQueryService &queries_;
};
