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

bool hasDocumentType(const QVector<LogisticsDocument> &documents, DocumentType type)
{
    return std::any_of(documents.cbegin(), documents.cend(), [type](const LogisticsDocument &item) {
        return item.type == type && item.status == DocumentStatus::Confirmed;
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
    const auto balances = queries_.batchLocationBalances(batch.id);
    if (!balances.ok())
        return Result<BatchWorkflowState>::failure(balances.error());
    state.currentLocations = balances.value();
    const auto remaining = queries_.batchRemaining(batch.id);
    if (!remaining.ok())
        return Result<BatchWorkflowState>::failure(remaining.error());
    state.currentQuantity = remaining.value();

    const auto product = masterData_.productTypeById(batch.productTypeId);
    if (!product.ok())
        return Result<BatchWorkflowState>::failure(product.error());
    state.productTypeName = product.value().typeCode;
    const auto category = masterData_.categoryById(product.value().categoryId);
    if (!category.ok())
        return Result<BatchWorkflowState>::failure(category.error());
    const auto documents = documents_.logisticsForBatch(batch.id);
    if (!documents.ok())
        return Result<BatchWorkflowState>::failure(documents.error());
    const auto locations = masterData_.allLocationsResult();
    if (!locations.ok())
        return Result<BatchWorkflowState>::failure(locations.error());
    QHash<EntityId, Location> locationsById;
    bool hasCompany = false;
    bool hasFactory = false;
    bool hasExternal = false;
    for (const auto &location : locations.value()) {
        locationsById.insert(location.id, location);
        if (!location.enabled)
            continue;
        if (location.countsAsInventory) {
            hasCompany = hasCompany || location.type == LocationType::Company;
            hasFactory = hasFactory || location.type == LocationType::Factory;
        } else if (location.type != LocationType::Company && location.type != LocationType::Factory) {
            hasExternal = true;
        }
    }
    bool hasNegative = false;
    bool companyHasStock = false;
    bool factoryHasStock = false;
    for (const auto &balance : state.currentLocations) {
        hasNegative = hasNegative || balance.quantity < 0;
        const auto location = locationsById.constFind(balance.locationId);
        if (location == locationsById.cend() || !location->enabled || !location->countsAsInventory)
            continue;
        companyHasStock = companyHasStock || (location->type == LocationType::Company && balance.quantity > 0);
        factoryHasStock = factoryHasStock || (location->type == LocationType::Factory && balance.quantity > 0);
    }
    const bool validProduct = product.value().enabled && category.value().enabled;
    const bool confirmedInbound = batch.status == DocumentStatus::Confirmed;
    const bool confirmedTransfer = hasDocumentType(documents.value(), DocumentType::Transfer);
    const bool confirmedOutbound = hasDocumentType(documents.value(), DocumentType::Outbound);
    const bool confirmedReturn = hasDocumentType(documents.value(), DocumentType::Return);
    const QString batchPrerequisite = !confirmedInbound
        ? QStringLiteral("入库已作废，可编辑并保存恢复生效")
        : !validProduct ? QStringLiteral("产品或分类已停用，新建记录前请先启用；已有记录仍可编辑") : QString();
    const QString transferPrerequisite = !batchPrerequisite.isEmpty() ? batchPrerequisite
        : !hasCompany || !hasFactory ? QStringLiteral("请准备已启用且计库存的公司和工厂库位") : QString();
    const QString outboundPrerequisite = !batchPrerequisite.isEmpty() ? batchPrerequisite
        : !hasFactory || !hasExternal ? QStringLiteral("请准备计库存的工厂及不计库存的 Amazon 目的地") : QString();
    const QString returnPrerequisite = transferPrerequisite;
    const bool canTransfer = transferPrerequisite.isEmpty();
    const bool canOutbound = outboundPrerequisite.isEmpty();
    const bool canReturn = returnPrerequisite.isEmpty();
    const bool validMasterData = validProduct && hasCompany && hasFactory && hasExternal;
    const bool finished = confirmedInbound && confirmedOutbound && state.currentQuantity == 0 && !hasNegative;

    state.steps.append(step(1, QStringLiteral("基础资料"),
        validMasterData ? WorkflowStepStatus::Completed : WorkflowStepStatus::NeedsAction,
        validMasterData ? QStringLiteral("产品及公司、工厂、外部目的地可用")
                        : QStringLiteral("请核对产品、分类和业务路线所需库位"),
        kMasterDataPage, QStringLiteral("去基础资料")));
    state.steps.append(step(2, QStringLiteral("公司入库"),
        confirmedInbound ? WorkflowStepStatus::Completed : WorkflowStepStatus::NeedsAction,
        confirmedInbound ? QStringLiteral("入库已生效，可随时编辑纠错")
                         : QStringLiteral("入库已作废，可编辑恢复"),
        kInboundPage, confirmedInbound ? QStringLiteral("编辑入库") : QStringLiteral("编辑并恢复入库"),
        confirmedInbound ? QString() : batchPrerequisite));
    state.steps.append(step(3, QStringLiteral("公司调拨到工厂"),
        confirmedTransfer ? WorkflowStepStatus::Completed : WorkflowStepStatus::NotStarted,
        confirmedTransfer ? QStringLiteral("已登记调拨，可继续补录或编辑")
                          : QStringLiteral("按实际公司到工厂的流转登记；账面库存不足也可补录"),
        kTransferPage, QStringLiteral("登记公司调拨"), transferPrerequisite));
    state.steps.append(step(4, QStringLiteral("工厂出库到 Amazon"),
        hasNegative ? WorkflowStepStatus::NeedsAction : finished ? WorkflowStepStatus::Completed
            : confirmedOutbound ? WorkflowStepStatus::InProgress : WorkflowStepStatus::NotStarted,
        hasNegative ? QStringLiteral("存在负库存，需核对或补录，不影响继续登记")
            : finished ? QStringLiteral("已出库，计库存库位余额为零")
            : confirmedOutbound ? QStringLiteral("已登记对外出库，可继续补录或编辑")
                                : QStringLiteral("按实际出库登记，可先记出库再补调拨"),
        kOutboundPage, QStringLiteral("登记工厂出库"), outboundPrerequisite));
    state.steps.append(step(5, QStringLiteral("工厂退回公司（可选）"),
        confirmedReturn ? WorkflowStepStatus::Completed : WorkflowStepStatus::NotApplicable,
        confirmedReturn ? QStringLiteral("已登记工厂退回，总库存不变")
                        : QStringLiteral("仅实际退回时登记，可按需跳过；允许补录"),
        kReturnPage, QStringLiteral("按需登记工厂退回"), returnPrerequisite));
    state.steps.append(step(6, QStringLiteral("继续流转 / 流程结束"),
        hasNegative || !confirmedInbound ? WorkflowStepStatus::NeedsAction
            : finished ? WorkflowStepStatus::Completed : WorkflowStepStatus::InProgress,
        hasNegative ? QStringLiteral("存在负库位，不能仅按总库存判断完成，请核对或补录")
            : !confirmedInbound ? QStringLiteral("入库已作废，可编辑恢复后继续流转")
            : finished ? QStringLiteral("批次流程已完成；仍可补录和纠错")
                       : QStringLiteral("当前账面库存 %1 件，可继续录入或核对原记录").arg(state.currentQuantity),
        kBatchTrackingPage, QStringLiteral("查看批次追踪")));

    state.availableActions = {WorkflowAction::GoToMasterData, WorkflowAction::EditInbound,
                              WorkflowAction::ViewTracking};
    if (canTransfer) state.availableActions.append(WorkflowAction::CreateTransfer);
    if (canOutbound) state.availableActions.append(WorkflowAction::CreateOutbound);
    if (canReturn) state.availableActions.append(WorkflowAction::CreateReturn);
    if (!confirmedInbound) {
        state.blockingReasons.append(batchPrerequisite);
        state.nextActionText = QStringLiteral("入库已作废，可编辑原记录并保存恢复生效");
        state.nextPage = kInboundPage;
        state.currentStep = 2;
    } else if (hasNegative) {
        state.blockingReasons.append(QStringLiteral("存在负库存，请核对或补录相关记录"));
        state.nextActionText = QStringLiteral("存在负库存，请核对或补录；仍可在业务页继续登记");
        state.nextPage = kBatchTrackingPage;
        state.currentStep = 6;
    } else if (!validProduct) {
        state.blockingReasons.append(batchPrerequisite);
        state.nextActionText = batchPrerequisite;
        state.nextPage = kMasterDataPage;
        state.currentStep = 1;
    } else if (finished) {
        state.nextActionText = QStringLiteral("查看批次追踪；需要纠错时仍可编辑原记录");
        state.nextPage = kBatchTrackingPage;
        state.currentStep = 6;
    } else if (factoryHasStock && canOutbound) {
        state.nextActionText = QStringLiteral("建议从工厂出库到 Amazon；工厂退回公司仅按需登记");
        state.nextPage = kOutboundPage;
        state.currentStep = 4;
    } else if (companyHasStock && canTransfer) {
        state.nextActionText = QStringLiteral("建议登记公司到工厂的调拨，也可直接补录其他已发生业务");
        state.nextPage = kTransferPage;
        state.currentStep = 3;
    } else if (!validMasterData) {
        state.blockingReasons.append(QStringLiteral("业务路线所需库位尚未准备齐全"));
        state.nextActionText = QStringLiteral("请补充公司、工厂或不计库存的 Amazon 目的地");
        state.nextPage = kMasterDataPage;
        state.currentStep = 1;
    } else {
        state.nextActionText = QStringLiteral("可查看追踪，或在业务页继续补录和纠错；零库存不限制登记");
        state.nextPage = kBatchTrackingPage;
        state.currentStep = 6;
    }
    state.overallStatus = hasNegative ? QStringLiteral("需核对")
        : !confirmedInbound ? QStringLiteral("已作废")
        : finished ? QStringLiteral("已完成")
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
    if (state.availableActions.contains(action))
        return Result<bool>::success(true);
    const int stepIndex = action == WorkflowAction::CreateTransfer ? 2
        : action == WorkflowAction::CreateOutbound ? 3 : 4;
    return Result<bool>::failure(blocked(state.steps.at(stepIndex).prerequisiteMessage));
}
