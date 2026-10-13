# 物流库存管理系统实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建可替代石墨表格的单机物流库存 Qt 应用，支持主数据、入库、内部调拨、最终出库、批次库存追踪、统计与 CSV 导出。

**Architecture:** 使用 Qt Widgets → Application Services → Repositories → SQLite 的轻量分层架构。库存只从不可变流水聚合，确认和取消在 `BEGIN IMMEDIATE` 事务中完成，UI 不直接执行 SQL。

**Tech Stack:** Qt 6（Core、Widgets、Sql、Test）、C++17、CMake 3.21+、SQLite。

## Global Constraints

- 目标平台为 Windows 11，源码和资源路径使用 UTF-8。
- 业务数量使用 `qint64` 正整数；流水变化使用非零 `qint64`。
- 草稿不影响库存；状态仅允许 `Draft → Confirmed → Cancelled`。
- 取消只能生成反向流水，已取消单据不可恢复。
- 第一版不包含登录权限、盘点调整、网络同步、财务金额和用途剩余库存。
- Service 集成测试必须使用临时 SQLite 数据库，不模拟 Repository。
- 未获用户明确授权时，执行者跳过所有 `git commit` 步骤，但仍执行指定的 `git diff --check`。

## File Structure

- `CMakeLists.txt`：项目、Qt 模块、测试和安装入口。
- `src/app/main.cpp`：应用启动、数据库迁移和主窗口创建。
- `src/app/application.h/.cpp`：组合数据库、Repository、Service 与页面。
- `src/core/models.h`：状态、单据类型、DTO 和查询结果结构。
- `src/core/result.h`：服务层可展示错误类型。
- `src/db/database.h/.cpp`：连接、事务和迁移。
- `src/db/migrations/001_initial.sql`：完整首版表结构、约束和索引。
- `src/repositories/master_data_repository.h/.cpp`：主数据持久化。
- `src/repositories/document_repository.h/.cpp`：入库及物流单据持久化。
- `src/repositories/inventory_repository.h/.cpp`：流水写入和库存聚合。
- `src/services/master_data_service.h/.cpp`：主数据规则。
- `src/services/inbound_service.h/.cpp`：入库草稿、确认与取消。
- `src/services/logistics_service.h/.cpp`：调拨/出库草稿、确认与取消。
- `src/services/inventory_query_service.h/.cpp`：库存和批次追踪查询。
- `src/services/report_service.h/.cpp`：统计和 CSV 导出。
- `src/ui/main_window.h/.cpp`：导航与页面容器。
- `src/ui/pages/*.h/.cpp`：八个独立页面。
- `src/ui/dialogs/*.h/.cpp`：主数据、入库和物流单据编辑对话框。
- `tests/test_support.h/.cpp`：临时数据库、迁移和固定数据。
- `tests/tst_*.cpp`：Repository、Service 与关键 UI 集成测试。

---

### Task 1: 项目骨架、数据库连接与迁移

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/db/database.h`
- Create: `src/db/database.cpp`
- Create: `src/db/migrations/001_initial.sql`
- Create: `tests/CMakeLists.txt`
- Create: `tests/test_support.h`
- Create: `tests/test_support.cpp`
- Test: `tests/tst_database.cpp`

**Interfaces:**
- Produces: `Database::open(path)`, `Database::transaction(mode, callback)`、`Database::migrate()`；首版完整 schema。
- Consumes: 无。

- [ ] **Step 1: 编写失败的迁移测试**

```cpp
void DatabaseTest::migratesEmptyDatabase()
{
    TestDatabase db;
    QVERIFY(db.openAndMigrate());
    QCOMPARE(db.scalar("SELECT version FROM schema_migrations"), QVariant(1));
    QCOMPARE(db.scalar("PRAGMA foreign_keys"), QVariant(1));
    QVERIFY(db.tableExists("inventory_entries"));
}
```

- [ ] **Step 2: 配置最小 CMake 并验证测试失败**

Run: `cmake -S . -B build -DBUILD_TESTING=ON && cmake --build build --config Debug && ctest --test-dir build -C Debug -R database --output-on-failure`
Expected: FAIL，原因是 `Database`、迁移或目标尚未实现。

- [ ] **Step 3: 实现数据库包装器**

```cpp
class Database {
public:
    explicit Database(QString connectionName);
    ~Database();
    bool open(const QString &path, QString *error);
    bool migrate(QString *error);
    QSqlDatabase connection() const;
    bool beginImmediate(QString *error);
    bool commit(QString *error);
    void rollback();
private:
    QString connectionName_;
};
```

`open()` 必须执行 `PRAGMA foreign_keys = ON`；`migrate()` 在事务中读取嵌入资源 `:/migrations/001_initial.sql`，逐条执行并写入 `schema_migrations(version)`。

- [ ] **Step 4: 编写首版 schema**

```sql
CREATE TABLE product_categories(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0,1)));
CREATE TABLE materials(id INTEGER PRIMARY KEY, category_id INTEGER NOT NULL REFERENCES product_categories(id), name TEXT NOT NULL, model_code TEXT NOT NULL UNIQUE, specification TEXT NOT NULL DEFAULT '', enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0,1)));
CREATE TABLE usage_types(id INTEGER PRIMARY KEY, code TEXT NOT NULL UNIQUE, name TEXT NOT NULL, description TEXT NOT NULL DEFAULT '', enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0,1)));
CREATE TABLE material_usage_types(material_id INTEGER NOT NULL REFERENCES materials(id), usage_type_id INTEGER NOT NULL REFERENCES usage_types(id), PRIMARY KEY(material_id, usage_type_id));
CREATE TABLE locations(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, type TEXT NOT NULL CHECK(type IN ('Company','Factory','Customer','ProjectSite','Other')), counts_as_inventory INTEGER NOT NULL CHECK(counts_as_inventory IN (0,1)), enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0,1)));
```

```sql
CREATE TABLE inbound_batches(
 id INTEGER PRIMARY KEY, batch_no TEXT NOT NULL UNIQUE, material_id INTEGER NOT NULL REFERENCES materials(id),
 quantity INTEGER NOT NULL CHECK(quantity > 0), initial_location_id INTEGER NOT NULL REFERENCES locations(id),
 business_at TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'Draft' CHECK(status IN ('Draft','Confirmed','Cancelled')),
 note TEXT NOT NULL DEFAULT '', material_name_snapshot TEXT, model_code_snapshot TEXT, location_name_snapshot TEXT,
 created_at TEXT NOT NULL, confirmed_at TEXT, cancelled_at TEXT);
CREATE TABLE logistics_documents(
 id INTEGER PRIMARY KEY, document_no TEXT NOT NULL UNIQUE, type TEXT NOT NULL CHECK(type IN ('Transfer','Outbound')),
 source_location_id INTEGER NOT NULL REFERENCES locations(id), destination_location_id INTEGER NOT NULL REFERENCES locations(id),
 business_at TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'Draft' CHECK(status IN ('Draft','Confirmed','Cancelled')),
 note TEXT NOT NULL DEFAULT '', source_name_snapshot TEXT, destination_name_snapshot TEXT,
 created_at TEXT NOT NULL, confirmed_at TEXT, cancelled_at TEXT, CHECK(source_location_id <> destination_location_id));
CREATE TABLE logistics_document_lines(
 id INTEGER PRIMARY KEY, document_id INTEGER NOT NULL REFERENCES logistics_documents(id) ON DELETE CASCADE,
 batch_id INTEGER NOT NULL REFERENCES inbound_batches(id), usage_type_id INTEGER NOT NULL REFERENCES usage_types(id),
 quantity INTEGER NOT NULL CHECK(quantity > 0), batch_no_snapshot TEXT, material_name_snapshot TEXT,
 model_code_snapshot TEXT, usage_code_snapshot TEXT, usage_name_snapshot TEXT);
CREATE TABLE inventory_entries(
 id INTEGER PRIMARY KEY, batch_id INTEGER NOT NULL REFERENCES inbound_batches(id), location_id INTEGER NOT NULL REFERENCES locations(id),
 source_type TEXT NOT NULL CHECK(source_type IN ('Inbound','Logistics')),
 source_id INTEGER NOT NULL, source_line_id INTEGER, quantity_delta INTEGER NOT NULL CHECK(quantity_delta <> 0),
 business_at TEXT NOT NULL, created_at TEXT NOT NULL,
 reversal_of_entry_id INTEGER UNIQUE REFERENCES inventory_entries(id));
CREATE INDEX idx_inventory_batch_location ON inventory_entries(batch_id, location_id);
CREATE INDEX idx_inventory_source ON inventory_entries(source_type, source_id);
CREATE INDEX idx_inventory_business_at ON inventory_entries(business_at);
CREATE INDEX idx_inventory_reversal ON inventory_entries(reversal_of_entry_id);
```

- [ ] **Step 5: 运行数据库测试**

Run: `cmake --build build --config Debug && ctest --test-dir build -C Debug -R database --output-on-failure`
Expected: PASS，测试确认迁移版本、外键、核心表、CHECK 和 UNIQUE 约束。

- [ ] **Step 6: 提交（获得授权时）**

```bash
git add CMakeLists.txt src/db tests/CMakeLists.txt tests/test_support.* tests/tst_database.cpp
git commit -m "feat: bootstrap database schema and migrations"
```

### Task 2: 核心类型与主数据服务

**Files:**
- Create: `src/core/models.h`
- Create: `src/core/result.h`
- Create: `src/repositories/master_data_repository.h`
- Create: `src/repositories/master_data_repository.cpp`
- Create: `src/services/master_data_service.h`
- Create: `src/services/master_data_service.cpp`
- Test: `tests/tst_master_data_service.cpp`

**Interfaces:**
- Consumes: `Database::connection()`。
- Produces: `MasterDataService::createCategory/createMaterial/createUsageType/createLocation/setMaterialUsages/setEnabled` 和 `enabled*()` 查询；`LocationType`、`DocumentStatus`、`DocumentType`。

- [ ] **Step 1: 编写主数据规则失败测试**

```cpp
void MasterDataServiceTest::disabledDataIsExcludedFromNewDocuments()
{
    const auto material = service.createMaterial(category.id, "网关", "GW-01", "");
    QVERIFY(material.ok());
    QVERIFY(service.setMaterialEnabled(material.value().id, false).ok());
    QVERIFY(std::none_of(service.enabledMaterials().begin(), service.enabledMaterials().end(),
                         [&](const Material &m) { return m.id == material.value().id; }));
    QCOMPARE(service.materialById(material.value().id).value().modelCode, QString("GW-01"));
}
```

- [ ] **Step 2: 运行并确认失败**

Run: `ctest --test-dir build -C Debug -R master_data --output-on-failure`
Expected: FAIL，缺少核心类型和服务实现。

- [ ] **Step 3: 定义精确类型和错误接口**

```cpp
enum class DocumentStatus { Draft, Confirmed, Cancelled };
enum class DocumentType { Transfer, Outbound };
enum class LocationType { Company, Factory, Customer, ProjectSite, Other };
struct ServiceError { QString code; QString message; QString field; };
template<class T> class Result { public: static Result success(T); static Result failure(ServiceError); bool ok() const; const T &value() const; const ServiceError &error() const; };
struct Material { qint64 id; qint64 categoryId; QString name; QString modelCode; QString specification; bool enabled; };
struct Location { qint64 id; QString name; LocationType type; bool countsAsInventory; bool enabled; };
```

- [ ] **Step 4: 实现 Repository 与 Service**

Repository 只负责参数化 SQL；Service 拒绝空名称/编码、重复业务键及不存在的关联。`enabled*()` 仅返回启用项，`*ById()` 必须能读取禁用历史资料。地点创建必须显式接收 `countsAsInventory`。

- [ ] **Step 5: 运行主数据和数据库测试**

Run: `cmake --build build --config Debug && ctest --test-dir build -C Debug -R "database|master_data" --output-on-failure`
Expected: PASS。

- [ ] **Step 6: 提交（获得授权时）**

```bash
git add src/core src/repositories/master_data_repository.* src/services/master_data_service.* tests/tst_master_data_service.cpp
git commit -m "feat: add configurable master data"
```

### Task 3: 库存 Repository 与入库生命周期

**Files:**
- Create: `src/repositories/inventory_repository.h`
- Create: `src/repositories/inventory_repository.cpp`
- Create: `src/repositories/document_repository.h`
- Create: `src/repositories/document_repository.cpp`
- Create: `src/services/inbound_service.h`
- Create: `src/services/inbound_service.cpp`
- Test: `tests/tst_inbound_service.cpp`

**Interfaces:**
- Consumes: `Database`、主数据查询、`Result<T>`。
- Produces: `InboundService::createDraft(const InboundDraft&)`、`updateDraft(id, draft)`、`confirm(id)`、`cancel(id)`；`InventoryRepository::quantity(batchId, locationId)`、`entriesForSource(type,id)`。

- [ ] **Step 1: 编写入库确认与幂等失败测试**

```cpp
void InboundServiceTest::confirmPostsExactlyOneEntry()
{
    const auto id = inbound.createDraft({"B-20260910-001", material.id, 820, company.id, businessAt, ""}).value();
    QVERIFY(inbound.confirm(id).ok());
    QCOMPARE(inventory.quantity(id, company.id), qint64(820));
    QCOMPARE(inventory.entriesForSource("Inbound", id).size(), 1);
    QVERIFY(!inbound.confirm(id).ok());
    QCOMPARE(inventory.entriesForSource("Inbound", id).size(), 1);
}
```

- [ ] **Step 2: 运行并确认失败**

Run: `ctest --test-dir build -C Debug -R inbound --output-on-failure`
Expected: FAIL，入库服务尚未实现。

- [ ] **Step 3: 实现草稿保存和确认**

```cpp
struct InboundDraft { QString batchNo; qint64 materialId; qint64 quantity; qint64 initialLocationId; QDateTime businessAt; QString note; };
Result<qint64> InboundService::createDraft(const InboundDraft &draft);
Result<void> InboundService::confirm(qint64 batchId);
```

`confirm()` 使用 `BEGIN IMMEDIATE`，检查 `Draft`、正整数、启用物料和库存地点，写一条正流水，保存物料/地点快照，以 `WHERE status='Draft'` 更新状态和 `confirmed_at`；任一步失败均 rollback。

- [ ] **Step 4: 编写取消约束失败测试**

```cpp
void InboundServiceTest::cancelRejectsWhenReversalWouldMakeStockNegative()
{
    const auto batch = fixtures.confirmInbound(100);
    fixtures.postConfirmedOutbound(batch, company.id, 1);
    const auto result = inbound.cancel(batch);
    QVERIFY(!result.ok());
    QCOMPARE(inventory.quantity(batch, company.id), qint64(99));
}
```

- [ ] **Step 5: 实现取消和反向流水**

`cancel()` 在写事务内读取未冲销原流水，计算每个 `(batch_id, location_id)` 的反向结果；任一结果 `< 0` 返回 `stock_dependency`。否则逐条写反向流水，将 `reversal_of_entry_id` 指向原记录，再以状态条件更新为 `Cancelled`。

- [ ] **Step 6: 运行测试**

Run: `cmake --build build --config Debug && ctest --test-dir build -C Debug -R "database|inbound" --output-on-failure`
Expected: PASS，覆盖草稿不影响库存、确认、重复确认、取消、重复取消和事务回滚。

- [ ] **Step 7: 提交（获得授权时）**

```bash
git add src/repositories/inventory_repository.* src/repositories/document_repository.* src/services/inbound_service.* tests/tst_inbound_service.cpp
git commit -m "feat: implement inbound inventory lifecycle"
```

### Task 4: 内部调拨和最终出库生命周期

**Files:**
- Create: `src/services/logistics_service.h`
- Create: `src/services/logistics_service.cpp`
- Modify: `src/repositories/document_repository.h`
- Modify: `src/repositories/document_repository.cpp`
- Test: `tests/tst_logistics_service.cpp`

**Interfaces:**
- Consumes: `InventoryRepository::quantity/insertEntry`、Document Repository、主数据查询。
- Produces: `LogisticsService::createDraft/updateDraft/confirm/cancel/copyToDraft`；`LogisticsDraft` 和 `LogisticsLineDraft`。

- [ ] **Step 1: 编写聚合超发失败测试**

```cpp
void LogisticsServiceTest::aggregatesSameBatchAcrossUsageLines()
{
    const auto batch = fixtures.confirmInbound(100);
    LogisticsDraft draft{"OUT-001", DocumentType::Outbound, company.id, customer.id, businessAt, "",
        {{batch, usageA.id, 60}, {batch, usageB.id, 50}}};
    const auto id = logistics.createDraft(draft).value();
    const auto result = logistics.confirm(id);
    QVERIFY(!result.ok());
    QCOMPARE(result.error().code, QString("insufficient_stock"));
    QCOMPARE(inventory.quantity(batch, company.id), qint64(100));
}
```

- [ ] **Step 2: 运行并确认失败**

Run: `ctest --test-dir build -C Debug -R logistics --output-on-failure`
Expected: FAIL，物流服务尚未实现。

- [ ] **Step 3: 定义并实现物流草稿**

```cpp
struct LogisticsLineDraft { qint64 batchId; qint64 usageTypeId; qint64 quantity; };
struct LogisticsDraft { QString documentNo; DocumentType type; qint64 sourceLocationId; qint64 destinationLocationId; QDateTime businessAt; QString note; QList<LogisticsLineDraft> lines; };
```

Service 校验至少一条明细、正整数、批次物料允许该用途；调拨两端均为不同库存地点；出库起点为库存地点、终点为启用外部地点。

- [ ] **Step 4: 实现确认流水**

在 `BEGIN IMMEDIATE` 中先按 `(batchId, sourceLocationId)` 聚合所有行并一次性校验库存。调拨每行写 `-quantity/+quantity`；出库每行只写 `-quantity`。每条明细保存批次、物料、用途与地点快照，最后条件更新状态。

- [ ] **Step 5: 编写调拨取消依赖测试**

```cpp
void LogisticsServiceTest::cannotCancelTransferAfterDestinationStockWasConsumed()
{
    const auto batch = fixtures.confirmInbound(500);
    const auto transfer = fixtures.confirmTransfer(batch, company.id, factory.id, 300);
    fixtures.confirmOutbound(batch, factory.id, customer.id, 1);
    QVERIFY(!logistics.cancel(transfer).ok());
    QCOMPARE(inventory.quantity(batch, factory.id), qint64(299));
}
```

- [ ] **Step 6: 实现取消与复制草稿**

取消沿用入库的反向流水算法；`copyToDraft(id)` 复制单据头和明细，清空 id、单号、确认/取消时间并设为 `Draft`，不复制流水。

- [ ] **Step 7: 运行物流测试**

Run: `cmake --build build --config Debug && ctest --test-dir build -C Debug -R "inbound|logistics" --output-on-failure`
Expected: PASS，覆盖调拨、出库、多用途聚合、地点约束、用途关联、冲销和快照。

- [ ] **Step 8: 提交（获得授权时）**

```bash
git add src/services/logistics_service.* src/repositories/document_repository.* tests/tst_logistics_service.cpp
git commit -m "feat: add transfer and outbound workflows"
```

### Task 5: 库存查询、批次追踪与统计导出

**Files:**
- Create: `src/services/inventory_query_service.h`
- Create: `src/services/inventory_query_service.cpp`
- Create: `src/services/report_service.h`
- Create: `src/services/report_service.cpp`
- Test: `tests/tst_inventory_queries.cpp`
- Test: `tests/tst_report_service.cpp`

**Interfaces:**
- Consumes: 数据库只读连接和已确认/冲销流水。
- Produces: `batchBalances()`、`locationBalances()`、`batchTimeline(batchId)`、`dashboardSummary(month)`、`usageShipmentSummary(range)`、`exportCsv(report, device)`。

- [ ] **Step 1: 编写 820 件端到端失败测试**

```cpp
void InventoryQueriesTest::tracksBatchAcrossLocations()
{
    const auto batch = fixtures.confirmInbound(820);
    fixtures.confirmTransfer(batch, company.id, factoryA.id, 500);
    fixtures.confirmTransfer(batch, company.id, factoryB.id, 320);
    fixtures.confirmOutbound(batch, factoryA.id, customer.id, 250);
    fixtures.confirmOutbound(batch, factoryB.id, project.id, 100);
    QCOMPARE(query.balance(batch, company.id), qint64(0));
    QCOMPARE(query.balance(batch, factoryA.id), qint64(250));
    QCOMPARE(query.balance(batch, factoryB.id), qint64(220));
    QCOMPARE(query.batchRemaining(batch), qint64(470));
}
```

- [ ] **Step 2: 运行并确认失败**

Run: `ctest --test-dir build -C Debug -R inventory_queries --output-on-failure`
Expected: FAIL，查询服务尚未实现。

- [ ] **Step 3: 实现聚合与时间线查询**

使用参数化 SQL 对 `inventory_entries` 的 `quantity_delta` 求和；地点库存仅纳入 `counts_as_inventory=1`。时间线按 `business_at, created_at, id` 排序，并返回来源单据、方向、数量、地点和快照。

- [ ] **Step 4: 编写 CSV 失败测试**

```cpp
void ReportServiceTest::exportsUtf8BomAndStableColumns()
{
    QByteArray bytes;
    QBuffer output(&bytes);
    output.open(QIODevice::WriteOnly);
    QVERIFY(reports.exportLocationInventory(&output).ok());
    QVERIFY(bytes.startsWith("\xEF\xBB\xBF"));
    QVERIFY(bytes.contains("批次号,物料型号,地点,库存数量\r\n"));
}
```

- [ ] **Step 5: 实现报表和 CSV**

工作台统计包括总库存、公司库存、工厂库存、本月入库、本月最终出库及最近业务。CSV 使用 UTF-8 BOM、CRLF、RFC 4180 双引号转义和 `yyyy-MM-dd HH:mm:ss` 时间格式；写入失败返回 `csv_write_failed`。

- [ ] **Step 6: 运行查询与报表测试**

Run: `cmake --build build --config Debug && ctest --test-dir build -C Debug -R "inventory_queries|report" --output-on-failure`
Expected: PASS。

- [ ] **Step 7: 提交（获得授权时）**

```bash
git add src/services/inventory_query_service.* src/services/report_service.* tests/tst_inventory_queries.cpp tests/tst_report_service.cpp
git commit -m "feat: add inventory tracking and reports"
```

### Task 6: 主窗口、工作台和业务页面

**Files:**
- Create: `src/app/main.cpp`
- Create: `src/app/application.h`
- Create: `src/app/application.cpp`
- Create: `src/ui/main_window.h`
- Create: `src/ui/main_window.cpp`
- Create: `src/ui/pages/dashboard_page.h`
- Create: `src/ui/pages/dashboard_page.cpp`
- Create: `src/ui/pages/inbound_page.h`
- Create: `src/ui/pages/inbound_page.cpp`
- Create: `src/ui/pages/logistics_page.h`
- Create: `src/ui/pages/logistics_page.cpp`
- Test: `tests/tst_main_window.cpp`

**Interfaces:**
- Consumes: 所有 Service 的查询和命令接口。
- Produces: `MainWindow` 八项导航框架；可用的工作台、入库、调拨和出库列表操作。

- [ ] **Step 1: 编写导航和刷新失败测试**

```cpp
void MainWindowTest::showsEightNavigationEntries()
{
    MainWindow window(services);
    auto *navigation = window.findChild<QListWidget *>("navigation");
    QVERIFY(navigation);
    QCOMPARE(navigation->count(), 8);
}
```

- [ ] **Step 2: 运行并确认失败**

Run: `ctest --test-dir build -C Debug -R main_window --output-on-failure`
Expected: FAIL，UI 尚未实现。

- [ ] **Step 3: 实现应用组合根和主窗口**

`Application` 按 Database → Repository → Service → MainWindow 的顺序持有对象。启动时数据库或迁移失败，用 `QMessageBox::critical` 显示原因并返回非零退出码。主窗口使用左侧 `QListWidget` 和右侧 `QStackedWidget`。

- [ ] **Step 4: 实现工作台和三个业务页面**

列表默认按 `business_at DESC`；页面按钮只调用 Service。草稿允许新增、编辑、删除；Confirmed 只允许查看和取消；Cancelled 允许复制为新草稿。成功命令发出 `dataChanged()`，主窗口刷新当前页和工作台。

- [ ] **Step 5: 运行 UI 测试**

Run: `cmake --build build --config Debug && ctest --test-dir build -C Debug -R main_window --output-on-failure`
Expected: PASS，覆盖导航、页面切换和数据变更刷新。

- [ ] **Step 6: 提交（获得授权时）**

```bash
git add src/app src/ui/main_window.* src/ui/pages/dashboard_page.* src/ui/pages/inbound_page.* src/ui/pages/logistics_page.* tests/tst_main_window.cpp
git commit -m "feat: add main window and business pages"
```

### Task 7: 入库和物流编辑对话框

**Files:**
- Create: `src/ui/dialogs/inbound_dialog.h`
- Create: `src/ui/dialogs/inbound_dialog.cpp`
- Create: `src/ui/dialogs/logistics_dialog.h`
- Create: `src/ui/dialogs/logistics_dialog.cpp`
- Modify: `src/ui/pages/inbound_page.cpp`
- Modify: `src/ui/pages/logistics_page.cpp`
- Test: `tests/tst_document_dialogs.cpp`

**Interfaces:**
- Consumes: `MasterDataService::enabled*()`、`InventoryQueryService`、入库/物流 Service。
- Produces: 可验证并保存 `InboundDraft` 和 `LogisticsDraft` 的模态对话框。

- [ ] **Step 1: 编写选择器过滤失败测试**

```cpp
void DocumentDialogsTest::batchPickerShowsOnlyPositiveStockAtSource()
{
    const auto available = fixtures.confirmInbound(10, company.id);
    const auto elsewhere = fixtures.confirmInbound(10, factory.id);
    LogisticsDialog dialog(services, DocumentType::Outbound);
    dialog.setSourceLocation(company.id);
    QCOMPARE(dialog.visibleBatchIds(), QList<qint64>{available});
    QVERIFY(!dialog.visibleBatchIds().contains(elsewhere));
}
```

- [ ] **Step 2: 运行并确认失败**

Run: `ctest --test-dir build -C Debug -R document_dialogs --output-on-failure`
Expected: FAIL。

- [ ] **Step 3: 实现入库对话框**

使用 `QFormLayout` 编辑批次号、物料、正整数数量、初始库存地点、业务时间和备注。保存失败时根据 `ServiceError.field` 聚焦控件并在控件旁显示错误；禁用资料不出现在新建选择器。

- [ ] **Step 4: 实现物流对话框和多明细表格**

单据头编辑单号、时间、发货地点、目的地点和备注；`QTableWidget` 明细支持批次、用途和数量。发货地点变化后仅加载该地点库存大于零的批次；批次变化后仅加载其物料允许的启用用途。

- [ ] **Step 5: 实现确认、取消二次确认和库存错误**

确认/取消使用 `QMessageBox::question`。库存不足信息必须包含批次号、发货地点、可用数量和申请数量；成功后关闭对话框并触发页面刷新。

- [ ] **Step 6: 运行对话框测试**

Run: `cmake --build build --config Debug && ctest --test-dir build -C Debug -R document_dialogs --output-on-failure`
Expected: PASS，覆盖字段定位、禁用资料过滤、批次和用途级联、多明细编辑。

- [ ] **Step 7: 提交（获得授权时）**

```bash
git add src/ui/dialogs src/ui/pages/inbound_page.cpp src/ui/pages/logistics_page.cpp tests/tst_document_dialogs.cpp
git commit -m "feat: add document editing dialogs"
```

### Task 8: 库存、批次、报表和基础资料页面

**Files:**
- Create: `src/ui/pages/inventory_page.h`
- Create: `src/ui/pages/inventory_page.cpp`
- Create: `src/ui/pages/batch_tracking_page.h`
- Create: `src/ui/pages/batch_tracking_page.cpp`
- Create: `src/ui/pages/reports_page.h`
- Create: `src/ui/pages/reports_page.cpp`
- Create: `src/ui/pages/master_data_page.h`
- Create: `src/ui/pages/master_data_page.cpp`
- Create: `src/ui/dialogs/master_data_dialog.h`
- Create: `src/ui/dialogs/master_data_dialog.cpp`
- Modify: `src/ui/main_window.cpp`
- Test: `tests/tst_query_pages.cpp`

**Interfaces:**
- Consumes: `InventoryQueryService`、`ReportService`、`MasterDataService`。
- Produces: 四个可查询页面、CSV 保存操作和完整主数据维护 UI。

- [ ] **Step 1: 编写批次追踪展示失败测试**

```cpp
void QueryPagesTest::trackingPageShowsTimelineAndDistribution()
{
    const auto batch = fixtures.createTrackedBatch();
    BatchTrackingPage page(query);
    page.selectBatch(batch);
    QVERIFY(page.findChild<QTableWidget *>("timeline")->rowCount() > 0);
    QVERIFY(page.findChild<QTableWidget *>("distribution")->rowCount() > 0);
}
```

- [ ] **Step 2: 运行并确认失败**

Run: `ctest --test-dir build -C Debug -R query_pages --output-on-failure`
Expected: FAIL。

- [ ] **Step 3: 实现库存查询和批次追踪**

库存页提供批次库存、地点库存和用途发货统计三个视图。追踪页显示批次概要、当前地点分布和按业务时间排序的流水时间线；历史字段读取快照。

- [ ] **Step 4: 实现报表与导出页面**

提供日期范围、报表类型和“导出 CSV”；使用 `QSaveFile` 保证失败时不留下半文件，调用 `ReportService::exportCsv()`，默认扩展名 `.csv`。

- [ ] **Step 5: 实现基础资料页面**

使用五个标签页维护类别、物料、用途、地点和型号用途关联。已被引用资料不硬删除，只允许禁用；禁用后历史详情仍可打开。

- [ ] **Step 6: 运行查询页测试**

Run: `cmake --build build --config Debug && ctest --test-dir build -C Debug -R query_pages --output-on-failure`
Expected: PASS，覆盖三类库存查询、时间线、CSV 保存失败和禁用历史展示。

- [ ] **Step 7: 提交（获得授权时）**

```bash
git add src/ui/pages/inventory_page.* src/ui/pages/batch_tracking_page.* src/ui/pages/reports_page.* src/ui/pages/master_data_page.* src/ui/dialogs/master_data_dialog.* src/ui/main_window.cpp tests/tst_query_pages.cpp
git commit -m "feat: complete inventory query and master data UI"
```

### Task 9: 整体验收、安装与发布构建

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/app/main.cpp`
- Create: `tests/tst_end_to_end.cpp`

**Interfaces:**
- Consumes: 全部应用接口。
- Produces: 可安装的 Windows GUI 程序和完整自动化验收信号。

- [ ] **Step 1: 编写完整验收测试**

```cpp
void EndToEndTest::allFailedCommandsAreAtomic()
{
    const auto batch = fixtures.confirmInbound(100);
    const auto document = fixtures.createOutboundDraft(batch, 101);
    QVERIFY(!services.logistics.confirm(document).ok());
    QCOMPARE(services.documents.status(document), DocumentStatus::Draft);
    QCOMPARE(services.inventory.quantity(batch, company.id), qint64(100));
    QVERIFY(services.inventory.entriesForSource("Logistics", document).isEmpty());
}
```

同一测试目标还需执行 820 件流转、重复确认、重复取消、主数据快照、出库不增加外部地点库存以及未提交事务回滚场景。

- [ ] **Step 2: 运行全量测试并确认新测试初始状态**

Run: `cmake --build build --config Debug && ctest --test-dir build -C Debug --output-on-failure`
Expected: 若有遗漏则 FAIL，并明确指出未满足的验收条件；不得放宽断言。

- [ ] **Step 3: 修复验收暴露的最小问题**

只修改导致失败的现有模块；保持 UI 无 SQL、Service 管事务、Repository 只持久化、库存仅从流水聚合这四条边界。每修复一个失败，重跑对应测试目标。

- [ ] **Step 4: 配置安装目标**

```cmake
install(TARGETS LogisticsInventory RUNTIME DESTINATION .)
qt_generate_deploy_app_script(TARGET LogisticsInventory OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${deploy_script})
```

应用数据目录使用 `QStandardPaths::AppLocalDataLocation`；首次启动自动创建目录和迁移数据库，不把数据库安装到程序目录。

- [ ] **Step 5: 执行最终验证**

Run: `cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON && cmake --build build-release --config Release && ctest --test-dir build-release -C Release --output-on-failure && cmake --install build-release --config Release --prefix dist`
Expected: 所有测试 PASS，`dist/LogisticsInventory.exe` 及 Qt 运行库存在。

Run: `git diff --check`
Expected: 无输出，退出码 0。

- [ ] **Step 6: 手工冒烟测试**

启动 `dist/LogisticsInventory.exe`，依次创建主数据、入库 820、调拨 500/320、出库 250/100；确认工作台和库存页显示公司 0、工厂 A 250、工厂 B 220、总库存 470，并导出可由 Excel 正确打开的中文 CSV。

- [ ] **Step 7: 提交（获得授权时）**

```bash
git add CMakeLists.txt src/app/main.cpp tests/tst_end_to_end.cpp
git commit -m "test: validate complete inventory workflow"
```

## Implementation Completion Criteria

- Debug 与 Release 全量 CTest 均通过。
- 设计文档第 14 节的八个重点验收场景均有自动化测试。
- 所有库存改变均可追溯到原流水或反向流水。
- UI 中不存在直接 SQL，库存命令不存在于 Repository 或 Widget。
- 新单据不显示禁用主数据，历史记录显示确认时快照。
- 安装目录可独立启动，数据库存放在用户应用数据目录。
