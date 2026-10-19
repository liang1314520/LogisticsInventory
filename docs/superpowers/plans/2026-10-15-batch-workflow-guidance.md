# 批次流程引导 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为每个批次增加持续显示的编号流程、前置条件提示、下一步跳转和分支状态，让用户能在入库、调拨、出库、退回和再次出库之间按真实业务进度操作。

**Architecture:** 新增只读的 `BatchWorkflowService`，从现有主数据、单据和库存流水实时计算 `BatchWorkflowState`，不保存手工步骤字段。`MainWindow` 持有当前批次上下文并渲染统一流程栏，各业务页通过共享上下文显示当前步骤；业务服务继续负责最终事务校验，页面只做体验层限制和跳转。

**Tech Stack:** Qt 6 Widgets, Qt Test, C++17, SQLite, CMake.

## Global Constraints

- 流程进度按具体批次计算，不能用全局单一状态代替多个批次状态。
- 草稿不算业务完成；只有确认单据和库存流水事实才能推进步骤。
- 库存仍按“批次 + 库位”计算，型号不拆成独立库存。
- 支持直接出库、先调拨再出库、出库后退回和退回后再次出库。
- UI 可以提示和限制，但 `InboundService`、`LogisticsService` 仍是最终业务授权层。
- 不自动创建或确认业务单据，不新增手工保存“当前步骤”的数据库字段。
- 历史单据、零库存批次和已停用主数据的历史快照必须继续可查看。
- 每个任务完成后运行其最窄相关测试，再运行受影响的全量测试；不要修改无关业务规则。

---

## 文件边界总览

### 新增文件

- `src/services/batch_workflow_service.h`: 定义流程步骤、批次状态、动作和查询接口。
- `src/services/batch_workflow_service.cpp`: 查询现有数据并计算流程状态、阻断原因和下一步。
- `src/ui/widgets/workflow_bar.h`: 流程栏控件接口，渲染批次选择、编号步骤和动作按钮。
- `src/ui/widgets/workflow_bar.cpp`: 流程栏布局、状态渲染和用户交互信号。
- `tests/tst_batch_workflow_service.cpp`: 流程状态服务的数据库级测试。
- `tests/tst_workflow_bar.cpp`: 流程栏和批次切换的 Qt Widgets 测试。

### 修改文件

- `src/core/models.h`: 仅在需要共享页面导航上下文类型时增加轻量枚举；优先避免把 UI 流程模型塞入领域模型。
- `src/repositories/document_repository.h/.cpp`: 增加参数化的 `logisticsForBatch(EntityId)` 查询，返回引用该批次的草稿和历史物流单据。
- `src/repositories/inventory_repository.h/.cpp`: 复用现有数量和流水查询；只有编译或测试证明缺少按批次聚合能力时，才增加最小参数化查询。
- `src/services/inventory_query_service.h/.cpp`: 暴露流程服务需要的批次摘要、当前库位和历史数量查询；保持现有查询结果不变。
- `src/services/batch_workflow_service.*`: 在 `logistics_master_data` 中编译和链接。
- `src/app/application.h/.cpp`: 创建并注入 `BatchWorkflowService`。
- `src/ui/main_window.h/.cpp`: 持有流程栏、当前批次、业务页面跳转和 mutation 后刷新逻辑。
- `src/ui/pages/inbound_page.h/.cpp`: 接收当前批次/流程上下文，显示步骤 02，支持跳转定位入库草稿。
- `src/ui/pages/logistics_page.h/.cpp`: 接收必需的 `BatchWorkflowService &`，区分调拨、出库和退回步骤，按当前批次提示可用操作并在新建前做 UI 层阻断。
- `src/ui/pages/batch_tracking_page.h/.cpp`: 支持从流程栏定位批次并刷新流程上下文。
- `src/ui/pages/master_data_page.h/.cpp`: 基础资料缺失时提供流程栏可跳转的稳定页面入口。
- `tests/CMakeLists.txt`: 注册两个新增测试目标。
- `tests/tst_main_window.cpp`: 覆盖流程栏、批次切换、下一步跳转和 mutation 后刷新。
- `tests/tst_query_pages.cpp`: 为直接构造 `MainWindow` 的测试创建 `BatchWorkflowService` 并传入新构造参数。
- `tests/tst_document_dialogs.cpp`: 覆盖当前批次预选/前置限制，不改变原对话框业务校验。

---

### Task 1: 定义流程状态契约与红测试

**Files:**
- Create: `src/services/batch_workflow_service.h`
- Create: `tests/tst_batch_workflow_service.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces `WorkflowStepStatus { NotStarted, InProgress, Completed, NotApplicable, NeedsAction }`.
- Produces `WorkflowAction { GoToMasterData, EditInbound, CreateTransfer, ConfirmTransfer, CreateOutbound, CreateReturn, ViewTracking }`.
- Produces `WorkflowBatchOption { EntityId batchId; QString batchNo; QString productTypeName; qint64 currentQuantity; DocumentStatus status; }`.
- Produces `WorkflowStep { int number; QString title; WorkflowStepStatus status; QString summary; QString prerequisiteMessage; int targetPage; QString actionText; }`.
- Produces `BatchWorkflowState { EntityId batchId; QString batchNo; QString productTypeName; qint64 currentQuantity; QVector<BatchLocationBalance> currentLocations; int currentStep; QString overallStatus; QVector<WorkflowStep> steps; QVector<WorkflowAction> availableActions; QStringList blockingReasons; QString nextActionText; int nextPage; }`.
- Produces `class BatchWorkflowService` with constructor `BatchWorkflowService(MasterDataService &, DocumentRepository &, InventoryRepository &, InventoryQueryService &)` and methods:
  - `Result<BatchWorkflowState> state(EntityId batchId) const;`
  - `Result<QVector<WorkflowBatchOption>> batchOptions() const;`
  - `Result<bool> canStartAction(EntityId batchId, WorkflowAction action) const;`

- [ ] **Step 1: Write the failing service test skeleton**

Create a QtTest fixture with `TestDatabase`, repositories, `MasterDataService`, `InboundService`, `LogisticsService`, `InventoryQueryService`, and `BatchWorkflowService`. Add tests with these exact scenarios:

```cpp
void draftInboundIsInProgressAndBlocksOutbound();
void confirmedInboundOffersDirectOutboundOrTransfer();
void confirmedTransferShowsDistributedStockAndOutboundNext();
void outboundThenReturnOffersReprocess();
void zeroBalanceHistoryRemainsTrackableAndCompleted();
void multipleBatchesHaveIndependentStates();
```

For the first test, create an inbound draft and assert `state.value().steps.at(1).status == WorkflowStepStatus::InProgress`, `availableActions` excludes `WorkflowAction::CreateOutbound`, and `nextPage == 1`.

- [ ] **Step 2: Register the narrow test target**

Add an executable to `tests/CMakeLists.txt`:

```cmake
add_executable(tst_batch_workflow_service
    tst_batch_workflow_service.cpp
    test_support.cpp
    test_support.h
)
target_link_libraries(tst_batch_workflow_service PRIVATE logistics_master_data Qt6::Test)
add_test(NAME batch_workflow_service COMMAND tst_batch_workflow_service)
```

- [ ] **Step 3: Run the red test**

Run:

```bash
cmake --build build --target tst_batch_workflow_service
ctest --test-dir build -R batch_workflow_service --output-on-failure
```

Expected: the red test fails to configure or compile because the new service implementation is absent; record the first compiler error before proceeding to Task 2.

- [ ] **Step 4: Keep the red-test checkpoint uncommitted**

Do not create a git commit unless the user explicitly requests one. Leave the contract and failing tests in the working tree for the next task.

---

### Task 2: Implement database-backed batch workflow state

**Files:**
- Modify: `src/services/batch_workflow_service.h`
- Create: `src/services/batch_workflow_service.cpp`
- Modify: `src/services/inventory_query_service.h/.cpp` to expose the existing batch location and historical-batch queries used by the workflow service without changing their SQL results.
- Modify: `src/repositories/document_repository.h/.cpp` to add a parameterized `logisticsForBatch(EntityId batchId)` query returning documents whose confirmed or draft lines reference the batch.
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes the exact types from Task 1.
- Produces deterministic state for the six steps: `01 基础资料`, `02 入库`, `03 库存安排`, `04 出库`, `05 退回处理`, `06 再次出库 / 流程结束`.
- Uses the existing `DocumentStatus`, `DocumentType`, `BatchBalance`, `BatchLocationBalance`, `ReturnableLine`, and inventory quantity APIs.

- [ ] **Step 1: Implement lookup helpers**

Add private helpers with exact responsibilities:

```cpp
Result<InboundBatch> loadBatch(EntityId batchId) const;
Result<QVector<LogisticsDocument>> loadDocumentsForBatch(EntityId batchId) const;
Result<bool> hasConfirmedInbound(const InboundBatch &) const;
Result<bool> hasConfirmedTransfer(const QVector<LogisticsDocument> &) const;
Result<bool> hasConfirmedOutbound(const QVector<LogisticsDocument> &) const;
Result<bool> hasConfirmedReturn(const QVector<LogisticsDocument> &) const;
```

Use existing repository list/by-id methods first. If a query would scan all documents, add one parameterized repository method rather than exposing SQL in the service.

- [ ] **Step 2: Implement step calculation**

Calculate statuses using facts, not page visits:

```cpp
steps[0] = valid enabled category/type/location ? Completed : NeedsAction;
steps[1] = Draft ? InProgress : Confirmed ? Completed : Cancelled ? NeedsAction : NotStarted;
steps[2] = confirmed transfer ? Completed : confirmed inbound && currentQuantity > 0 ? NotApplicable : InProgress;
steps[3] = currentQuantity > 0 && confirmed inbound ? NeedsAction : confirmed outbound ? Completed : NotStarted;
steps[4] = returnable quantity > 0 ? NeedsAction : confirmed outbound ? NotApplicable : NotStarted;
steps[5] = currentQuantity == 0 && no draft/dependency ? Completed : confirmed return && currentQuantity > 0 ? NeedsAction : InProgress;
```

Keep the implementation explicit and document the branch precedence in code comments only where necessary. For zero-balance history, retain `ViewTracking` as an available action even when step 06 is completed.

- [ ] **Step 3: Implement next-action selection**

Use this deterministic precedence:

```text
invalid master data -> GoToMasterData
inbound draft -> EditInbound
logistics transfer draft -> ConfirmTransfer
confirmed inbound with inventory > 0 and no blocking draft -> CreateOutbound
confirmed inbound with no confirmed transfer and no blocking draft -> CreateTransfer
returnable quantity > 0 -> CreateReturn
zero inventory and no blocking work -> ViewTracking
```

Set `nextPage` to the existing navigation indices: master data `7`, inbound `1`, transfer `2`, outbound `3`, batch tracking `5`, return `8`.

- [ ] **Step 4: Implement batch list and action checks**

`batchOptions()` must return draft, confirmed, and cancelled historical batches ordered consistently with `InventoryQueryService::historicalBatches()`, including zero-balance history. The workflow bar may show all options, while downstream logistics creation continues to use the existing `availableBatches(locationId)` inventory query so drafts and zero-balance batches cannot be selected for new stock movement.

`canStartAction()` must reject actions with a `ServiceError` containing a user-facing prerequisite message. It must not modify data.

- [ ] **Step 5: Run service tests**

Run:

```bash
cmake --build build --target tst_batch_workflow_service
ctest --test-dir build -R batch_workflow_service --output-on-failure
```

Expected: all six workflow state tests PASS.

- [ ] **Step 6: Run related regression tests**

Run:

```bash
ctest --test-dir build -R "(end_to_end|inventory_queries|master_data|logistics)" --output-on-failure
```

Expected: all selected existing tests PASS and inventory totals remain unchanged.

---

### Task 3: Add the reusable workflow bar widget

**Files:**
- Create: `src/ui/widgets/workflow_bar.h`
- Create: `src/ui/widgets/workflow_bar.cpp`
- Create: `tests/tst_workflow_bar.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes `BatchWorkflowService`, `BatchWorkflowState`, `BatchBalance`, and `WorkflowAction`.
- Produces signals:
  - `void batchChanged(EntityId batchId);`
  - `void nextRequested(int pageIndex, EntityId batchId);`
  - `void trackingRequested(EntityId batchId);`
- Produces methods:
  - `Result<bool> refresh();`
  - `Result<bool> setBatch(EntityId batchId);`
  - `EntityId currentBatchId() const;`

- [ ] **Step 1: Write widget tests first**

Add tests:

```cpp
void rendersSixNumberedSteps();
void listsBatchesAndEmitsBatchChanged();
void nextButtonUsesWorkflowTarget();
void trackingButtonEmitsCurrentBatch();
void clearsStateWhenNoBatchExists();
```

Assertions must use stable object names:

```text
workflowBar
workflowBatchSelector
workflowStatusLabel
workflowSteps
workflowNextButton
workflowTrackingButton
workflowMessageLabel
```

- [ ] **Step 2: Implement widget layout**

Use a compact `QVBoxLayout` containing:

1. `QHBoxLayout`: current batch `QComboBox`, overall status `QLabel`;
2. `QTableWidget` or read-only six-row step list named `workflowSteps`;
3. `QLabel` for next-action/blocking message;
4. `QHBoxLayout` with `workflowNextButton` and `workflowTrackingButton`.

Do not add a second decorative card or alter the existing navigation labels.

- [ ] **Step 3: Implement rendering and signals**

On selector change, call `BatchWorkflowService::state()` and emit `batchChanged`. On next click, emit `nextRequested(state.nextPage, currentBatchId())`; on tracking click emit `trackingRequested(currentBatchId())`. Disable actions when no batch or when no available action exists.

- [ ] **Step 4: Run widget tests**

Run:

```bash
cmake --build build --target tst_workflow_bar
ctest --test-dir build -R workflow_bar --output-on-failure
```

Expected: all widget tests PASS.

---

### Task 4: Integrate workflow context into application and main window

**Files:**
- Modify: `src/app/application.h/.cpp`
- Modify: `src/ui/main_window.h/.cpp`
- Modify: `tests/tst_main_window.cpp`

**Interfaces:**
- `Application` owns `std::unique_ptr<BatchWorkflowService> workflowService_` and passes it to `MainWindow`.
- Extend `MainWindow` constructor to:

```cpp
MainWindow(MasterDataService &, InventoryQueryService &, InboundService &,
           LogisticsService &, BatchWorkflowService &, QWidget *parent = nullptr);
```

- Add `MainWindow::setCurrentBatch(EntityId)` and `MainWindow::currentBatchId() const`.
- Add stable child object name `workflowBar`.

- [ ] **Step 1: Add failing main-window tests**

Add tests:

```cpp
void workflowBarIsAlwaysVisible();
void selectingBatchUpdatesWorkflowBar();
void mutationRefreshesWorkflowState();
void nextActionNavigatesToExpectedPage();
```

Use the existing `TestDatabase` setup and create a confirmed batch. Assert `findChild<QWidget *>(QStringLiteral("workflowBar"))` is non-null, set the current batch, and assert `currentBatchId()` and the batch selector's current data match the created batch.

- [ ] **Step 2: Inject the service and create the bar**

Add a forward declaration and reference member for `BatchWorkflowService` in `main_window.h`. In the constructor, create `WorkflowBar(*workflowService, central)`, set its object name to `workflowBar`, place it above the existing horizontal navigation/pages layout, and connect its signals:

```cpp
connect(workflowBar_, &WorkflowBar::batchChanged, this,
        [this](EntityId id) { setCurrentBatch(id); });
connect(workflowBar_, &WorkflowBar::nextRequested, this,
        [this](int page, EntityId id) {
            setCurrentBatch(id);
            navigation_->setCurrentRow(page);
        });
connect(workflowBar_, &WorkflowBar::trackingRequested, this,
        [this](EntityId id) {
            setCurrentBatch(id);
            navigation_->setCurrentRow(5);
        });
```

The workflow bar must be visible regardless of the selected navigation row. Its `refresh()` must run after initial page construction and after every successful inbound, transfer, outbound, return, or master-data mutation.

- [ ] **Step 3: Implement current-batch propagation**

`setCurrentBatch(id)` stores the ID, calls `workflowBar_->setBatch(id)`, and emits a private/current-batch signal if page context needs it. `currentBatchId()` returns the stored ID. Do not duplicate workflow state in each page.

When `refreshAfterMutation()` runs, refresh the workflow bar before refreshing the current page so the next-action text reflects the committed database state.

- [ ] **Step 4: Update application construction**

In `Application::initialize()`, construct `workflowService_` after `queryService_` and pass `*workflowService_` into the new `MainWindow` constructor. Keep destruction order valid by declaring `workflowService_` after `queryService_` and before `mainWindow_`.

- [ ] **Step 5: Run main-window tests**

Run:

```bash
cmake --build build --target tst_main_window
ctest --test-dir build -R main_window --output-on-failure
```

Expected: the existing nine-navigation tests and the four new workflow-bar tests PASS.

---

### Task 5: Add page-level workflow context, precondition messaging, and navigation targeting

**Files:**
- Modify: `src/ui/pages/inbound_page.h/.cpp`
- Modify: `src/ui/pages/logistics_page.h/.cpp`
- Modify: `src/ui/pages/batch_tracking_page.h/.cpp`
- Modify: `src/ui/pages/master_data_page.h/.cpp`
- Modify: `tests/tst_document_dialogs.cpp`
- Modify: `tests/tst_main_window.cpp`

**Interfaces:**
- Add non-owning page context methods:
  - `void setCurrentBatch(EntityId batchId);`
  - `EntityId currentBatchId() const;`
  - `void setWorkflowMessage(const QString &message);`
- `InboundPage` exposes `Result<bool> selectBatch(EntityId batchId);`.
- `LogisticsPage` exposes `Result<bool> selectBatch(EntityId batchId);` and uses its existing `DocumentType type_` to enforce transfer/outbound/return-specific action checks.
- Pages continue to use their existing service methods for all writes.

- [ ] **Step 1: Add failing page tests**

Add these tests to `tst_main_window.cpp` and `tst_document_dialogs.cpp`:

```cpp
void inboundPageSelectsWorkflowBatch();
void logisticsPageSelectsWorkflowBatch();
void outboundCreateShowsInboundPrerequisite();
void returnPageShowsNoReturnableQuantity();
void historyViewRemainsAvailableWhenWorkflowBlocksNewAction();
```

For `outboundCreateShowsInboundPrerequisite()`, use a draft-only inbound batch, set it as current, invoke the outbound page's create action, and assert the page error label contains `QStringLiteral("先确认入库")` and no logistics draft was inserted.

For `historyViewRemainsAvailableWhenWorkflowBlocksNewAction()`, use a zero-balance confirmed batch and assert the batch tracking page can still select and render its timeline.

- [ ] **Step 2: Add page context widgets**

At the top of each affected page, add a compact read-only `QLabel` named `workflowContext` with text formatted as:

```text
步骤 NN / 06：标题
前置：...
当前操作：...
下一步：...
```

Keep the existing tables and action buttons unchanged. The context label is informational; `MainWindow` remains the owner of the workflow bar.

- [ ] **Step 3: Implement batch selection**

`InboundPage::selectBatch(batchId)` searches the existing documents table by the batch ID stored in `Qt::UserRole` (add that role during `refresh()`), selects the matching row, and returns `not_found` if no row matches.

`LogisticsPage::selectBatch(batchId)` searches loaded document lines through `service_.byId()` for a matching batch, selects the matching row, and returns `not_found` if the page has no such document. The method must not create or mutate a document.

`BatchTrackingPage::selectBatch()` already exists; expose its current selection to `MainWindow` without changing its historical query semantics.

- [ ] **Step 4: Enforce UI preconditions before opening create dialogs**

Before `LogisticsPage::openCreateDialog()` opens a dialog, call `workflowService_.canStartAction(currentBatchId_, actionForType())`. If the result fails, set the page error label to the returned message, leave the dialog unopened, and return the failure. If no current batch is selected, preserve the existing unrestricted page behavior so users can create a document by choosing a batch manually.

Map page types exactly:

```cpp
DocumentType::Transfer -> WorkflowAction::CreateTransfer;
DocumentType::Outbound -> WorkflowAction::CreateOutbound;
DocumentType::Return -> WorkflowAction::CreateReturn;
```

Do not disable viewing, editing a selected draft, cancelling a confirmed document, or copying a cancelled document solely because the workflow bar blocks a new action.

- [ ] **Step 5: Add current-batch filtering/selection to dialogs**

Extend `LogisticsDialog::initialize()` with an optional `EntityId preferredBatchId = 0` parameter. After loading locations and source batches, select `preferredBatchId` in the first line only when it is present and available at the selected source location. For return dialogs, use the returnable-line list and preserve the existing model selection behavior.

Extend `InboundDialog::initialize()` only as needed to load a selected draft; do not auto-create a new draft.

- [ ] **Step 6: Run page-level tests**

Run:

```bash
cmake --build build --target tst_document_dialogs tst_main_window
ctest --test-dir build -R "(document_dialogs|main_window)" --output-on-failure
```

Expected: all existing dialog/page tests and the new precondition/navigation tests PASS.

---

### Task 6: Add complete workflow integration coverage

**Files:**
- Modify: `tests/tst_batch_workflow_service.cpp`
- Modify: `tests/tst_main_window.cpp`
- Modify: `tests/tst_end_to_end.cpp`
- Modify: `tests/CMakeLists.txt` to keep the end-to-end target linked with `logistics_master_data` and add any required workflow-service source only through the library target, not by duplicating implementation files.

**Interfaces:**
- Consumes the service and UI interfaces from Tasks 1–5.
- Produces executable acceptance coverage for direct outbound, transfer, return, reprocess, cancellation, and multiple batches.

- [ ] **Step 1: Add direct-outbound acceptance test**

Create a confirmed inbound batch in a company inventory location and an external customer location. Assert the workflow state exposes `CreateOutbound`, the next page is `3`, confirming an outbound marks step 04 as completed, and the remaining quantity decreases by the outbound amount.

- [ ] **Step 2: Add transfer acceptance test**

Create a confirmed inbound batch, confirm transfers to two inventory locations, and assert:

```cpp
QCOMPARE(state.currentLocations.size(), 2);
QCOMPARE(state.steps.at(2).status, WorkflowStepStatus::Completed);
QCOMPARE(state.nextPage, 3);
```

Then confirm separate outbound documents and verify existing inventory totals remain the same as the pre-workflow implementation.

- [ ] **Step 3: Add return/reprocess acceptance test**

Use the existing `splitShipmentReturnAndReprocess()` data pattern with F25 and P25 models. After confirming the return, assert `WorkflowAction::CreateOutbound` is available, the current quantity equals the returned quantity, and after reprocessing the remaining quantity and timeline remain correct.

- [ ] **Step 4: Add cancellation and dependency coverage**

Assert that a confirmed inbound or transfer with downstream stock dependency remains blocked by the existing service error, while the workflow state recomputes from the unchanged database facts. After cancelling downstream documents in the required order, assert the workflow state updates without manual reset.

- [ ] **Step 5: Add independent multi-batch coverage**

Create two confirmed batches with different quantities and process only one. Assert that selecting the untouched batch shows its own inbound/next-action state and never displays the first batch's locations, quantity, or next page.

- [ ] **Step 6: Run all integration tests**

Run:

```bash
cmake --build build --target tst_batch_workflow_service tst_main_window tst_end_to_end
ctest --test-dir build -R "(batch_workflow_service|main_window|end_to_end)" --output-on-failure
```

Expected: all workflow and end-to-end tests PASS.

---

### Task 7: Final regression, static review, and manual workflow check

**Files:**
- Modify only files identified by failing tests or compile diagnostics from Tasks 1–6.
- Do not add unrelated refactors or schema migrations.

**Interfaces:**
- No new public interfaces; this task verifies the integrated feature against the approved spec.

- [ ] **Step 1: Build the complete application and test suite**

Run:

```bash
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Expected: build succeeds and every registered test passes.

- [ ] **Step 2: Review the complete diff**

Run:

```bash
git diff --check
git diff --stat
git status --short
```

Check manually that:

- no workflow state is stored in SQLite;
- no existing inventory SQL was changed unnecessarily;
- no page can confirm a document without the existing service validation;
- all new strings are Chinese and consistent with current UI labels;
- no temporary files, build outputs, or credentials are included.

- [ ] **Step 3: Perform the manual user path**

Launch `build/LogisticsInventory.exe` and execute:

```text
基础资料 -> 建立分类、产品类型、F25/P25、公司仓、工厂A、客户现场
入库管理 -> 新建 B-2026-001 / 820 -> 保存 -> 确认
流程栏 -> 选择 B-2026-001 -> 查看下一步为调拨或出库
调拨管理 -> 公司仓到工厂A 500 -> 确认
调拨管理 -> 公司仓到工厂B 320 -> 确认
出库管理 -> 工厂A到客户现场 / F25 / 500 -> 确认
出库管理 -> 工厂B到客户现场 / P25 / 320 -> 确认
退回管理 -> 客户现场到公司仓 / F25 / 50 -> 确认
出库管理 -> 公司仓到项目现场 / F25 / 30 -> 确认
批次追踪 -> 检查完整时间线和剩余20
```

Verify after each confirmation that the flow bar updates without restart, the next-action button targets the expected page, and blocked operations show a prerequisite message instead of creating an invalid draft.

- [ ] **Step 4: Report completion without committing**

Unless the user explicitly asks for a commit, leave changes in the working tree and report the exact test command and result. Do not claim completion if any build, test, or manual verification step fails.
