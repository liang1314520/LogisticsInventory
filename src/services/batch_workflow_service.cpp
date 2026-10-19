#include "batch_workflow_service.h"

#include "repositories/document_repository.h"
#include "repositories/inventory_repository.h"
#include "services/master_data_service.h"

#include <QHash>

#include <algorithm>

namespace {
constexpr int kMasterDataPage = 7;
constexpr int kInboundPage = 1;
constexpr int kTransferPage = 2;
constexpr int kOutboundPage = 3;
constexpr int kBatchTrackingPage = 5;
constexpr int kReturnPage = 8;

ServiceError blocked(const QString &message)
{
    return {QStringLiteral("workflow_blocked"), message, QStringLiteral("batchId")};
}

bool hasDocumentType(const QVector<LogisticsDocument> &documents, DocumentType type,
                     const QHash<EntityId, Location> &locations)
{
    return std::any_of(documents.cbegin(), documents.cend(), [&](const LogisticsDocument &item) {
        if (item.type != type || item.status != DocumentStatus::Confirmed)
            return false;
        const auto source = locations.constFind(item.sourceLocationId);
        const auto destination = locations.constFind(item.destinationLocationId);
        if (source == locations.cend() || destination == locations.cend() || !source->countsAsInventory)
            return false;
        switch (type) {
        case DocumentType::Transfer:
            return source->type == LocationType::Company && destination->type == LocationType::Factory &&
                destination->countsAsInventory;
        case DocumentType::Outbound:
            return source->type == LocationType::Factory && !destination->countsAsInventory &&
                destination->type != LocationType::Company && destination->type != LocationType::Factory;
        case DocumentType::Return:
            return source->type == LocationType::Factory && destination->type == LocationType::Company &&
                destination->countsAsInventory;
        }
        return false;
    });
}

bool hasDraftType(const QVector<LogisticsDocument> &documents, DocumentType type)
{
    return std::any_of(documents.cbegin(), documents.cend(), [type](const LogisticsDocument &item) {
        return item.type == type && item.status == DocumentStatus::Draft;
    });
}

WorkflowStep step(int number, const QString &title, WorkflowStepStatus status,
                  const QString &summary, int targetPage, const QString &actionText,
                  const QString &prerequisite = {})
{
    return {number, title, status, summary, prerequisite, targetPage, actionText};
}
}

BatchWorkflowService::BatchWorkflowService(MasterDataService &masterData,
                                           DocumentRepository &documents,
                                           InventoryRepository &inventory,
                                           InventoryQueryService &queries)
    : masterData_(masterData), documents_(documents), inventory_(inventory), queries_(queries)
{
}

Result<BatchWorkflowState> BatchWorkflowService::state(EntityId batchId) const
{
    const auto batchResult = documents_.inboundById(batchId);
    if (!batchResult.ok())
        return Result<BatchWorkflowState>::failure(batchResult.error());
    const InboundBatch &batch = batchResult.value();

    BatchWorkflowState state;
    state.batchId = batch.id;
    state.batchNo = batch.batchNo;
    const auto locationBalances = queries_.batchLocationBalances(batch.id);
    if (!locationBalances.ok())
        return Result<BatchWorkflowState>::failure(locationBalances.error());
    state.currentLocations = locationBalances.value();
    const auto remaining = queries_.batchRemaining(batch.id);
    if (!remaining.ok())
        return Result<BatchWorkflowState>::failure(remaining.error());
    state.currentQuantity = remaining.value();

    const auto productType = masterData_.productTypeById(batch.productTypeId);
    if (!productType.ok())
        return Result<BatchWorkflowState>::failure(productType.error());
    state.productTypeName = productType.value().typeCode;
    const auto category = masterData_.categoryById(productType.value().categoryId);
    if (!category.ok())
        return Result<BatchWorkflowState>::failure(category.error());

    const auto initialLocation = masterData_.locationById(batch.initialLocationId);
    if (!initialLocation.ok())
        return Result<BatchWorkflowState>::failure(initialLocation.error());

    const auto documents = documents_.logisticsForBatch(batch.id);
    if (!documents.ok())
        return Result<BatchWorkflowState>::failure(documents.error());

    const auto locations = masterData_.allLocationsResult();
    if (!locations.ok())
        return Result<BatchWorkflowState>::failure(locations.error());
    QHash<EntityId, Location> locationsById;
    bool hasCompany = false;
    bool hasFactory = false;
    bool hasExternalDestination = false;
    for (const Location &location : locations.value()) {
        locationsById.insert(location.id, location);
        if (!location.enabled)
            continue;
        if (location.countsAsInventory) {
            hasCompany = hasCompany || location.type == LocationType::Company;
            hasFactory = hasFactory || location.type == LocationType::Factory;
        } else if (location.type != LocationType::Company && location.type != LocationType::Factory) {
            hasExternalDestination = true;
        }
    }
    qint64 companyQuantity = 0;
    qint64 factoryQuantity = 0;
    for (const auto &balance : state.currentLocations) {
        const auto location = locationsById.constFind(balance.locationId);
        if (location == locationsById.cend() || !location->enabled || !location->countsAsInventory)
            continue;
        if (location->type == LocationType::Company)
            companyQuantity += balance.quantity;
        else if (location->type == LocationType::Factory)
            factoryQuantity += balance.quantity;
    }
    const bool canTransfer = companyQuantity > 0 && hasFactory;
    const bool canOutbound = factoryQuantity > 0 && hasExternalDestination;
    const bool canReturn = factoryQuantity > 0 && hasCompany;

    const bool validMasterData = productType.value().enabled && category.value().enabled &&
        initialLocation.value().enabled && initialLocation.value().countsAsInventory &&
        initialLocation.value().type == LocationType::Company;
    const bool confirmedInbound = batch.status == DocumentStatus::Confirmed;
    const bool confirmedTransfer = hasDocumentType(documents.value(), DocumentType::Transfer, locationsById);
    const bool confirmedOutbound = hasDocumentType(documents.value(), DocumentType::Outbound, locationsById);
    const bool confirmedReturn = hasDocumentType(documents.value(), DocumentType::Return, locationsById);
    const bool transferDraft = hasDraftType(documents.value(), DocumentType::Transfer);
    const bool outboundDraft = hasDraftType(documents.value(), DocumentType::Outbound);
    const bool returnDraft = hasDraftType(documents.value(), DocumentType::Return);

    const WorkflowStepStatus masterStatus = validMasterData
        ? WorkflowStepStatus::Completed : WorkflowStepStatus::NeedsAction;
    state.steps.append(step(1, QStringLiteral("基础资料"), masterStatus,
                            validMasterData ? QStringLiteral("主数据可用")
                                            : QStringLiteral("存在停用或无效主数据"),
                            kMasterDataPage, QStringLiteral("去基础资料"),
                            validMasterData ? QString() : QStringLiteral("请启用产品类型、分类，并使用计库存的公司初始库位")));

    WorkflowStepStatus inboundStatus = WorkflowStepStatus::NotStarted;
    QString inboundSummary = QStringLiteral("尚未登记入库");
    QString inboundAction;
    if (batch.status == DocumentStatus::Draft) {
        inboundStatus = WorkflowStepStatus::InProgress;
        inboundSummary = QStringLiteral("入库草稿待确认");
        inboundAction = QStringLiteral("去编辑入库");
    } else if (batch.status == DocumentStatus::Confirmed) {
        inboundStatus = WorkflowStepStatus::Completed;
        inboundSummary = QStringLiteral("入库已确认");
    } else {
        inboundStatus = WorkflowStepStatus::NeedsAction;
        inboundSummary = QStringLiteral("入库已取消");
        inboundAction = QStringLiteral("重新创建入库");
    }
    state.steps.append(step(2, QStringLiteral("公司入库"), inboundStatus, inboundSummary,
                            kInboundPage, inboundAction,
                            confirmedInbound ? QString() : QStringLiteral("请先确认公司入库")));

    const QString transferPrerequisite = !confirmedInbound ? QStringLiteral("请先确认公司入库")
        : !hasFactory ? QStringLiteral("请先准备已启用且计库存的工厂库位")
        : companyQuantity <= 0 ? QStringLiteral("公司暂无该批次可调拨库存") : QString();
    WorkflowStepStatus arrangementStatus = WorkflowStepStatus::NotStarted;
    QString arrangementSummary = QStringLiteral("等待公司入库确认");
    QString arrangementAction;
    if (transferDraft) {
        arrangementStatus = WorkflowStepStatus::InProgress;
        arrangementSummary = QStringLiteral("存在公司发往工厂的调拨草稿");
        arrangementAction = QStringLiteral("去确认调拨");
    } else if (confirmedInbound && companyQuantity > 0) {
        arrangementStatus = WorkflowStepStatus::NeedsAction;
        arrangementSummary = QStringLiteral("公司还有 %1 件，需调拨到工厂后才能出库").arg(companyQuantity);
        if (canTransfer)
            arrangementAction = QStringLiteral("去调拨到工厂");
    } else if (confirmedTransfer) {
        arrangementStatus = WorkflowStepStatus::Completed;
        arrangementSummary = QStringLiteral("已完成公司到工厂的调拨");
    }
    state.steps.append(step(3, QStringLiteral("公司调拨到工厂"), arrangementStatus,
                            arrangementSummary, kTransferPage, arrangementAction, transferPrerequisite));

    const QString outboundPrerequisite = !confirmedInbound ? QStringLiteral("请先确认公司入库")
        : factoryQuantity <= 0 ? QStringLiteral("工厂暂无该批次可用库存，请先从公司调拨到工厂")
        : !hasExternalDestination ? QStringLiteral("请先准备不计库存的海外 Amazon 目的地") : QString();
    WorkflowStepStatus outboundStatus = WorkflowStepStatus::NotStarted;
    QString outboundSummary = QStringLiteral("等待调拨到工厂后出库");
    QString outboundAction;
    if (outboundDraft) {
        outboundStatus = WorkflowStepStatus::InProgress;
        outboundSummary = QStringLiteral("存在工厂发往 Amazon 的出库草稿");
        outboundAction = QStringLiteral("去确认出库");
    } else if (confirmedOutbound && state.currentQuantity == 0) {
        outboundStatus = WorkflowStepStatus::Completed;
        outboundSummary = QStringLiteral("已全部出库，公司和工厂库存均为零");
    } else if (confirmedInbound && factoryQuantity > 0) {
        outboundStatus = WorkflowStepStatus::NeedsAction;
        outboundSummary = QStringLiteral("工厂还有 %1 件可安排发往 Amazon").arg(factoryQuantity);
        if (canOutbound)
            outboundAction = QStringLiteral("去创建工厂出库");
    } else if (confirmedOutbound) {
        outboundStatus = WorkflowStepStatus::InProgress;
        outboundSummary = QStringLiteral("已部分出库，剩余库存需先调拨到工厂");
    }
    state.steps.append(step(4, QStringLiteral("工厂出库到 Amazon"), outboundStatus, outboundSummary,
                            kOutboundPage, outboundAction, outboundPrerequisite));

    const QString returnPrerequisite = !confirmedInbound ? QStringLiteral("请先确认公司入库")
        : factoryQuantity <= 0 ? QStringLiteral("工厂暂无该批次可退库存")
        : !hasCompany ? QStringLiteral("请先准备已启用且计库存的接收公司库位") : QString();
    WorkflowStepStatus returnStatus = confirmedReturn ? WorkflowStepStatus::Completed
                                                     : WorkflowStepStatus::NotApplicable;
    QString returnSummary = confirmedReturn ? QStringLiteral("已登记工厂退回，总库存不变")
                                             : QStringLiteral("可选环节，没有实际退回可跳过");
    QString returnAction;
    if (returnDraft) {
        returnStatus = WorkflowStepStatus::InProgress;
        returnSummary = QStringLiteral("存在工厂退回草稿");
        returnAction = QStringLiteral("去确认工厂退回");
    } else if (canReturn) {
        returnSummary = QStringLiteral("工厂可用 %1 件；仅实际退回公司时登记，不影响总库存")
                            .arg(factoryQuantity);
        returnAction = QStringLiteral("按需登记工厂退回");
    }
    state.steps.append(step(5, QStringLiteral("工厂退回公司（可选）"), returnStatus, returnSummary,
                            kReturnPage, returnAction, returnPrerequisite));

    const bool hasBlockingDraft = batch.status == DocumentStatus::Draft || transferDraft ||
        outboundDraft || returnDraft;
    const bool finished = state.currentQuantity == 0 && !hasBlockingDraft &&
        (confirmedOutbound || batch.status == DocumentStatus::Cancelled);
    const auto completionStatus = finished ? WorkflowStepStatus::Completed : WorkflowStepStatus::InProgress;
    const QString completionSummary = finished ? QStringLiteral("批次流程已完成")
        : QStringLiteral("当前总库存 %1 件；继续公司调拨、工厂出库，退回仅按需登记")
              .arg(state.currentQuantity);
    state.steps.append(step(6, QStringLiteral("继续流转 / 流程结束"), completionStatus,
                            completionSummary, kBatchTrackingPage,
                            finished ? QStringLiteral("查看批次追踪") : QStringLiteral("继续处理")));

    if (!validMasterData) {
        state.availableActions.append(WorkflowAction::GoToMasterData);
        state.blockingReasons.append(QStringLiteral("请启用产品类型、分类，并使用计库存的公司初始库位"));
        state.nextActionText = QStringLiteral("请先完成基础资料");
        state.nextPage = kMasterDataPage;
        state.currentStep = 1;
    } else if (batch.status == DocumentStatus::Draft) {
        state.availableActions.append(WorkflowAction::EditInbound);
        state.blockingReasons.append(QStringLiteral("入库尚未确认"));
        state.nextActionText = QStringLiteral("请先确认入库");
        state.nextPage = kInboundPage;
        state.currentStep = 2;
    } else if (transferDraft) {
        state.availableActions.append(WorkflowAction::ConfirmTransfer);
        state.blockingReasons.append(QStringLiteral("存在待确认的调拨草稿"));
        state.nextActionText = QStringLiteral("请先确认或删除调拨草稿");
        state.nextPage = kTransferPage;
        state.currentStep = 3;
    } else if (outboundDraft) {
        state.availableActions.append(WorkflowAction::CreateOutbound);
        state.blockingReasons.append(QStringLiteral("存在待确认的出库草稿"));
        state.nextActionText = QStringLiteral("请先确认或删除出库草稿");
        state.nextPage = kOutboundPage;
        state.currentStep = 4;
    } else if (returnDraft) {
        state.availableActions.append(WorkflowAction::CreateReturn);
        state.blockingReasons.append(QStringLiteral("存在待确认的退回草稿"));
        state.nextActionText = QStringLiteral("请先确认或删除退回草稿");
        state.nextPage = kReturnPage;
        state.currentStep = 5;
    } else if (confirmedInbound && state.currentQuantity > 0) {
        if (canTransfer)
            state.availableActions.append(WorkflowAction::CreateTransfer);
        if (canOutbound)
            state.availableActions.append(WorkflowAction::CreateOutbound);
        if (canReturn)
            state.availableActions.append(WorkflowAction::CreateReturn);
        if (canOutbound) {
            state.nextActionText = QStringLiteral("从工厂出库到海外 Amazon 仓；工厂退回公司仅按需登记");
            state.nextPage = kOutboundPage;
            state.currentStep = 4;
        } else if (canTransfer) {
            state.nextActionText = QStringLiteral("先将公司库存调拨到工厂，再从工厂出库到 Amazon");
            state.nextPage = kTransferPage;
            state.currentStep = 3;
        } else {
            state.availableActions.append(WorkflowAction::GoToMasterData);
            state.nextActionText = factoryQuantity > 0 ? outboundPrerequisite
                : companyQuantity > 0 ? transferPrerequisite
                : QStringLiteral("该批次库存不在可用的公司或工厂库位，请检查库位类型、启用和计库存设置");
            if (!canReturn)
                state.blockingReasons.append(state.nextActionText);
            state.nextPage = kMasterDataPage;
            state.currentStep = 1;
        }
    } else {
        state.availableActions.append(WorkflowAction::ViewTracking);
        state.nextActionText = finished ? QStringLiteral("查看批次追踪")
            : QStringLiteral("当前批次没有可用库存，请核对批次追踪中的单据和库存流水");
        if (!finished)
            state.blockingReasons.append(state.nextActionText);
        state.nextPage = kBatchTrackingPage;
        state.currentStep = 6;
    }

    state.overallStatus = finished ? QStringLiteral("已完成")
        : state.blockingReasons.isEmpty() ? QStringLiteral("进行中") : QStringLiteral("需处理");
    return Result<BatchWorkflowState>::success(state);
}

Result<QVector<WorkflowBatchOption>> BatchWorkflowService::batchOptions() const
{
    const auto batches = documents_.inboundList();
    if (!batches.ok())
        return Result<QVector<WorkflowBatchOption>>::failure(batches.error());
    QVector<WorkflowBatchOption> options;
    options.reserve(batches.value().size());
    for (const InboundBatch &batch : batches.value()) {
        QString productTypeName = batch.typeCodeSnapshot;
        const auto productType = masterData_.productTypeById(batch.productTypeId);
        if (productType.ok())
            productTypeName = productType.value().typeCode;
        const auto quantity = queries_.batchRemaining(batch.id);
        if (!quantity.ok())
            return Result<QVector<WorkflowBatchOption>>::failure(quantity.error());
        options.append({batch.id, batch.batchNo, productTypeName, quantity.value(), batch.status,
                        batch.materialModel});
    }
    return Result<QVector<WorkflowBatchOption>>::success(options);
}

Result<bool> BatchWorkflowService::canStartAction(EntityId batchId, WorkflowAction action) const
{
    const auto current = state(batchId);
    if (!current.ok())
        return Result<bool>::failure(current.error());
    const auto &state = current.value();
    const bool createsDocument = action == WorkflowAction::CreateTransfer ||
        action == WorkflowAction::CreateOutbound || action == WorkflowAction::CreateReturn;
    if (createsDocument && !state.blockingReasons.isEmpty())
        return Result<bool>::failure(blocked(state.nextActionText));
    if (state.availableActions.contains(action))
        return Result<bool>::success(true);

    QString message = state.nextActionText;
    if (createsDocument) {
        const int stepIndex = action == WorkflowAction::CreateTransfer ? 2
            : action == WorkflowAction::CreateOutbound ? 3 : 4;
        if (!state.steps.at(stepIndex).prerequisiteMessage.isEmpty())
            message = state.steps.at(stepIndex).prerequisiteMessage;
    }
    return Result<bool>::failure(blocked(message));
}
