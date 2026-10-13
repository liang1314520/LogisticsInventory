# 产品类型库存简化 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将“产品类别—物料—用途”改造成“产品分类—产品类型—批次”库存系统，自动迁移现有数据并彻底移除用途概念。

**Architecture:** 保持 `Qt Widgets → Application Services → Repositories → SQLite` 分层。先用事务化 v2 迁移建立新表并保持实体 ID、批次、单据、流水与库存合计，再从底层到界面替换类型和接口；每个任务都先写失败测试，再实现最小改动。

**Tech Stack:** C++17、Qt 6.10、Qt Widgets、Qt SQL、Qt Test、SQLite、CMake/Ninja。

## Global Constraints

- 产品模型固定为“产品分类 → 产品类型”，产品类型字段仅包含所属分类、类型编号、可选说明、启用状态。
- 类型编号只要求在同一分类内唯一，不同分类可以使用相同编号。
- 保留批次、库存库位、内部调拨、外部目的地、批次追踪、不可变流水和取消冲销。
- 调拨和出库明细只包含批次与正整数数量，同一单据同一批次最多一条。
- 彻底移除用途表、用途字段、用途快照、用途界面和用途统计。
- 现有数据库必须自动迁移；迁移失败必须完整回滚，不删除用户数据库。
- 历史展示使用确认时的产品分类名称、类型编号、批次号和地点名称快照。
- 不增加序列号、金额、供应商、客户档案、盘点、手工调整、登录权限或网络同步。
- 未经用户明确授权不得创建 Git commit；以下 commit 步骤仅在获得授权后执行。

---

## File Map

- `src/db/migrations/002_product_types.sql`：事务内重建产品类型、批次、物流明细和流水结构。
- `src/db/database.cpp`、`src/db/migrations/migrations.qrc`：按版本顺序运行 001 和 002。
- `src/core/models.h`：定义 `ProductType`，删除 `Material` 与 `UsageType`。
- `src/repositories/master_data_repository.*`、`src/services/master_data_service.*`：产品分类、产品类型和地点 API。
- `src/repositories/document_repository.*`：批次、物流明细和新快照持久化。
- `src/services/inbound_service.*`、`src/services/logistics_service.*`：简化确认与取消规则。
- `src/services/inventory_query_service.*`：产品类型、库位、批次和明细报表查询。
- `src/ui/dialogs/master_data_dialog.*`、`src/ui/dialogs/inbound_dialog.*`、`src/ui/dialogs/logistics_dialog.*`：移除用途控件并统一“产品类型”文案。
- `src/ui/pages/master_data_page.*`、`src/ui/pages/inventory_page.*`、`src/ui/pages/batch_tracking_page.cpp`、`src/ui/pages/reports_page.cpp`、`src/ui/pages/dashboard_page.cpp`：改造主数据、库存、追踪、报表和工作台。
- `tests/tst_database.cpp`、`tests/tst_master_data_service.cpp`、`tests/tst_inbound_service.cpp`、`tests/tst_logistics_service.cpp`、`tests/tst_inventory_queries.cpp`、`tests/tst_report_service.cpp`、`tests/tst_query_pages.cpp`、`tests/tst_document_dialogs.cpp`、`tests/tst_main_window.cpp`、`tests/tst_end_to_end.cpp`：迁移、服务、对话框、页面与 820 件端到端回归。

### Task 1: 事务化 v2 数据库迁移

**Files:**
- Create: `src/db/migrations/002_product_types.sql`
- Modify: `src/db/migrations/migrations.qrc`
- Modify: `src/db/database.cpp:127-158`
- Modify: `src/db/database.h`
- Modify: `tests/tst_database.cpp`
- Modify: `tests/test_support.h`
- Modify: `tests/test_support.cpp`

**Interfaces:**
- Consumes: v1 的 `materials`、`usage_types`、`material_usage_types`、`inbound_batches`、`logistics_document_lines` 和 `inventory_entries`。
- Produces: schema version 2；`product_types(id,category_id,type_code,description,enabled)`；批次使用 `product_type_id`；物流明细只包含批次、数量和分类/类型快照。

- [ ] **Step 1: 为测试支持增加分阶段迁移接口**

在 `TestDatabase` 增加：

```cpp
bool open();
bool migrate();
bool executeResource(const QString &resourcePath);
```

`openAndMigrate()` 调用 `open()` 和 `migrate()`。`executeResource()` 使用 `QFile` 读取资源、按分号拆分，并直接用 `QSqlQuery(database_.connection())` 执行每条语句，使测试能够先装载 v1 schema 再调用生产迁移器；不暴露 `Database::execute()`。

在 `Database` 增加私有 helper：

```cpp
bool executeMigration(const QString &resourcePath, QString *error);
bool foreignKeyCheck(QString *error);
```

它负责读取并执行单个迁移资源，`migrate()` 仍是唯一公开的生产迁移入口。

- [ ] **Step 2: 写迁移失败测试**

在 `tests/tst_database.cpp` 先把全部既有 schema 测试改为 v2：空库版本期望为 2；核心表期望包含 `product_types` 且不含三张旧表；正数、唯一、可空草稿编号、零流水约束全部改用 `product_type_id` 和新快照列，不能只追加迁移用例。然后增加：

```cpp
void DatabaseTest::migratesV1DataToProductTypes()
{
    TestDatabase db;
    QVERIFY(db.open());
    QVERIFY(db.executeResource(
        QStringLiteral(":/migrations/001_initial.sql")));
    QVERIFY(db.execute("INSERT INTO schema_migrations(version) VALUES(1)"));
    QVERIFY(db.execute("INSERT INTO product_categories(id,name) VALUES(1,'保险箱')"));
    QVERIFY(db.execute("INSERT INTO materials(id,category_id,name,model_code,specification) VALUES(1,1,'旧保险箱','F25','说明')"));
    QVERIFY(db.execute("INSERT INTO usage_types(id,code,name) VALUES(1,'OLD','旧用途')"));
    QVERIFY(db.execute("INSERT INTO locations(id,name,type,counts_as_inventory) VALUES(1,'公司仓','Company',1)"));
    QVERIFY(db.execute("INSERT INTO inbound_batches(id,batch_no,material_id,quantity,initial_location_id,business_at,status,created_at) VALUES(1,'B-1',1,100,1,'2026-09-01','Confirmed','2026-09-01')"));
    QVERIFY(db.execute("INSERT INTO inventory_entries(batch_id,location_id,source_type,source_id,quantity_delta,business_at,created_at) VALUES(1,1,'Inbound',1,80,'2026-09-01','2026-09-01')"));
    QVERIFY(db.migrate());
    QCOMPARE(db.scalar("SELECT MAX(version) FROM schema_migrations"), QVariant(2));
    QCOMPARE(db.scalar("SELECT type_code FROM product_types WHERE id=1"), QVariant("F25"));
    QVERIFY(!db.tableExists("materials"));
    QVERIFY(!db.tableExists("usage_types"));
    QVERIFY(!db.tableExists("material_usage_types"));
    QCOMPARE(db.scalar("SELECT SUM(quantity_delta) FROM inventory_entries"), QVariant(80));
}
```

测试夹具必须包含：一个已确认入库批次；一张物流单中同一批次按两个用途拆成两行；分别引用两条旧明细的库存流水；一组带 `reversal_of_entry_id` 的取消流水。迁移后断言：

```cpp
QCOMPARE(db.scalar("SELECT MAX(version) FROM schema_migrations"), QVariant(2));
QCOMPARE(db.scalar("SELECT type_code FROM product_types WHERE id=1"), QVariant("F25"));
QCOMPARE(db.scalar("SELECT COUNT(*) FROM logistics_document_lines WHERE document_id=1"), QVariant(1));
QCOMPARE(db.scalar("SELECT quantity FROM logistics_document_lines WHERE document_id=1"), QVariant(100));
QCOMPARE(db.scalar("SELECT COUNT(*) FROM inventory_entries"), oldEntryCount);
QCOMPARE(db.scalar("SELECT reversal_of_entry_id FROM inventory_entries WHERE id=5"), QVariant(4));
QCOMPARE(db.scalar("SELECT COUNT(*) FROM pragma_foreign_key_check"), QVariant(0));
QVERIFY(!db.tableExists("materials"));
QVERIFY(!db.tableExists("usage_types"));
QVERIFY(!db.tableExists("material_usage_types"));
```

迁移前先记录每个 `(batch_id,location_id)` 的余额和每张物流单的明细数量总和，迁移后逐项比较；还要断言旧明细对应的每条 `inventory_entries.source_line_id` 都指向聚合后的 `MIN(id)`。

另加 `migrationFailureRollsBackV1()`：装载合法 v1 后预先创建一个名为 `line_id_map` 的 TEMP 表。002 会先完成 `product_types`、`inbound_batches_v2`、`logistics_document_lines_v2` 的创建与数据复制，直到中段再次执行 `CREATE TEMP TABLE line_id_map` 时因同名 TEMP 对象确定失败。断言 `migrate()` 返回 false、version 仍为 1、`product_types` 与所有 `*_v2` 表均不存在、全部旧表仍存在、旧表 schema/行数和逐批次/库位余额未变化、流水及冲销关系未变化、连接仍可查询。

- [ ] **Step 3: 运行 RED**

Run:

```bash
cmake --build build/Qt_6_10_0_MinGW_64_bit-Debug --target tst_database
ctest --test-dir build/Qt_6_10_0_MinGW_64_bit-Debug -R '^database$' --output-on-failure
```

Expected: FAIL，因为 v2 迁移和 `product_types` 尚不存在。

- [ ] **Step 4: 编写迁移 SQL**

`002_product_types.sql` 必须创建以下最终结构；`batch_no` 与 `document_no` 保持可空，以支持尚未编号的草稿：

```sql
CREATE TABLE product_types(
 id INTEGER PRIMARY KEY,
 category_id INTEGER NOT NULL REFERENCES product_categories(id),
 type_code TEXT NOT NULL,
 description TEXT NOT NULL DEFAULT '',
 enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0,1)),
 UNIQUE(category_id,type_code)
);

CREATE TABLE inbound_batches_v2(
 id INTEGER PRIMARY KEY,
 batch_no TEXT UNIQUE,
 product_type_id INTEGER NOT NULL REFERENCES product_types(id),
 quantity INTEGER NOT NULL CHECK(quantity > 0),
 initial_location_id INTEGER NOT NULL REFERENCES locations(id),
 business_at TEXT NOT NULL,
 status TEXT NOT NULL DEFAULT 'Draft' CHECK(status IN ('Draft','Confirmed','Cancelled')),
 note TEXT NOT NULL DEFAULT '',
 category_name_snapshot TEXT,
 type_code_snapshot TEXT,
 location_name_snapshot TEXT,
 created_at TEXT NOT NULL,
 confirmed_at TEXT,
 cancelled_at TEXT
);

CREATE TABLE logistics_document_lines_v2(
 id INTEGER PRIMARY KEY,
 document_id INTEGER NOT NULL REFERENCES logistics_documents(id) ON DELETE CASCADE,
 batch_id INTEGER NOT NULL REFERENCES inbound_batches_v2(id),
 quantity INTEGER NOT NULL CHECK(quantity > 0),
 batch_no_snapshot TEXT,
 category_name_snapshot TEXT,
 type_code_snapshot TEXT,
 UNIQUE(document_id,batch_id)
);

CREATE TABLE inventory_entries_v2(
 id INTEGER PRIMARY KEY,
 batch_id INTEGER NOT NULL REFERENCES inbound_batches_v2(id),
 location_id INTEGER NOT NULL REFERENCES locations(id),
 source_type TEXT NOT NULL CHECK(source_type IN ('Inbound','Logistics')),
 source_id INTEGER NOT NULL,
 source_line_id INTEGER,
 quantity_delta INTEGER NOT NULL CHECK(quantity_delta <> 0),
 business_at TEXT NOT NULL,
 created_at TEXT NOT NULL,
 reversal_of_entry_id INTEGER UNIQUE REFERENCES inventory_entries_v2(id)
);

INSERT INTO product_types(id,category_id,type_code,description,enabled)
SELECT id,category_id,model_code,specification,enabled FROM materials;
```

复制批次时将 `material_id` 映射为 `product_type_id`，并从原分类名称和 `model_code_snapshot`/当前 `model_code` 生成确认快照。按依赖逆序删除旧表：先流水，再物流明细，再批次，最后用途关联、用途和物料；随后重命名新表，并重建 001 中四个 `inventory_entries` 索引。

旧数据可能把同一批次按多个用途拆成多行。迁移时必须按 `(document_id,batch_id)` 聚合，使用 `MIN(id)` 作为新明细 ID、`SUM(quantity)` 作为数量，并建立临时映射：

```sql
CREATE TEMP TABLE line_id_map AS
SELECT old.id AS old_id, grouped.new_id
FROM logistics_document_lines old
JOIN (
  SELECT document_id,batch_id,MIN(id) AS new_id
  FROM logistics_document_lines
  GROUP BY document_id,batch_id
) grouped USING(document_id,batch_id);
```

复制库存流水时把 `source_line_id` 通过 `line_id_map` 映射到聚合后的明细 ID；流水本身的 ID、数量变化和冲销关系保持不变。复制前创建 TEMP 审计表保存批次数、单据数、流水数、每个批次/地点余额、每张物流单的总数量；重建后使用 `SELECT CASE WHEN ... THEN 1 ELSE RAISE(ABORT,'migration integrity check failed') END` 逐项比较，并执行 `PRAGMA foreign_key_check`，有结果则由迁移器返回失败。所有 TEMP 对象在事务结束前删除。

由于 SQLite 的 `RAISE()` 只能用于 trigger，实际 SQL 采用临时校验表的 `CHECK(ok=1)` 约束：

```sql
CREATE TEMP TABLE migration_assert(ok INTEGER NOT NULL CHECK(ok=1));
INSERT INTO migration_assert
SELECT COUNT(*)=(SELECT batch_count FROM migration_audit)
FROM inbound_batches_v2;
```

对单据数、流水数、逐批次/库位余额和逐单据数量分别执行同类 `INSERT`；任一比较为 false 都触发约束错误并回滚。`Database::migrate()` 在执行完 002 后、写 version 2 前使用 `PRAGMA foreign_key_check` 查询并拒绝任何返回行。

- [ ] **Step 5: 让迁移器按版本加载资源**

在 `migrations.qrc` 注册 002；在 `Database` 实现 `executeMigration()` 和 `foreignKeyCheck()`，并将 `Database::migrate()` 改成：

```cpp
const QStringList resources{
    QStringLiteral(":/migrations/001_initial.sql"),
    QStringLiteral(":/migrations/002_product_types.sql")
};
if (current > resources.size()) {
    if (error) *error = QStringLiteral("Database schema version %1 is newer than supported version %2")
                            .arg(current).arg(resources.size());
    return false;
}
for (int version=current+1; version<=resources.size(); ++version) {
    if (!executeMigration(resources.at(version-1), error)) return false;
    if (version == 2 && !foreignKeyCheck(error)) return false;
    if (!execute(QStringLiteral("INSERT INTO schema_migrations(version) VALUES(%1)").arg(version), error)) return false;
}
```

- [ ] **Step 6: 运行 GREEN**

Run: 上述 database 测试命令。
Expected: database 测试全部 PASS，version 为 2。

- [ ] **Step 7: Commit（仅获授权后）**

```bash
git add src/db/migrations/002_product_types.sql src/db/migrations/migrations.qrc src/db/database.h src/db/database.cpp tests/tst_database.cpp tests/test_support.*
git commit -m "feat: migrate inventory data to product types"
```

### Task 2: 一次替换产品类型、文档模型与业务服务

**Files:**
- Modify: `src/core/models.h`
- Modify: `src/repositories/master_data_repository.h`
- Modify: `src/repositories/master_data_repository.cpp`
- Modify: `src/repositories/document_repository.h`
- Modify: `src/repositories/document_repository.cpp`
- Modify: `src/services/master_data_service.h`
- Modify: `src/services/master_data_service.cpp`
- Modify: `src/services/inbound_service.h`
- Modify: `src/services/inbound_service.cpp`
- Modify: `src/services/logistics_service.h`
- Modify: `src/services/logistics_service.cpp`
- Modify: `tests/tst_master_data_service.cpp`
- Modify: `tests/tst_inbound_service.cpp`
- Modify: `tests/tst_logistics_service.cpp`

**Interfaces:**
- Produces:

```cpp
struct ProductType {
    EntityId id=0;
    EntityId categoryId=0;
    QString typeCode;
    QString description;
    bool enabled=true;
};
struct LogisticsLineDraft { EntityId batchId=0; qint64 quantity=0; };
struct LogisticsLine : LogisticsLineDraft {
    EntityId id=0;
    EntityId productTypeId=0;
    QString batchNoSnapshot;
    QString categoryNameSnapshot;
    QString typeCodeSnapshot;
};
struct InboundDraft {
    QString batchNo;
    EntityId productTypeId=0;
    qint64 quantity=0;
    EntityId initialLocationId=0;
    QDateTime businessAt;
    QString note;
};
struct InboundBatch : InboundDraft {
    EntityId id=0;
    DocumentStatus status=DocumentStatus::Draft;
    QString categoryNameSnapshot;
    QString typeCodeSnapshot;
    QString locationNameSnapshot;
    QDateTime createdAt;
    QDateTime confirmedAt;
    QDateTime cancelledAt;
};

Result<ProductType> productTypeById(EntityId id) const;
Result<QVector<ProductType>> enabledProductTypesResult() const;
Result<QVector<ProductType>> allProductTypesResult() const;
Result<ProductType> createProductType(EntityId categoryId,const QString &typeCode,const QString &description);
Result<ProductType> updateProductType(EntityId id,EntityId categoryId,const QString &typeCode,const QString &description);
Result<bool> setProductTypeEnabled(EntityId id,bool enabled);
Result<bool> confirmInbound(EntityId id,const QString &categoryName,
    const QString &typeCode,const QString &locationName,const QDateTime &confirmedAt);
Result<bool> updateLogisticsLineSnapshots(EntityId lineId,const QString &batchNo,
    const QString &categoryName,const QString &typeCode);
```

`enabledProductTypesResult()` 必须联结 `product_categories`，同时过滤 `product_types.enabled=1` 与 `product_categories.enabled=1`；`productTypeById()` 不做启用过滤，供历史读取。

- [ ] **Step 1: 写服务层失败测试**

覆盖同分类重复拒绝、不同分类相同编号允许、trim、缺失分类、停用类型过滤、停用分类过滤和历史按 ID 可读；同时把 inbound/logistics 夹具从 `{materialId,usageId}` 改为 `productTypeId` 与 `{batchId,quantity}`，增加重复批次及快照断言：

```cpp
const auto safe=service->createCategory("保险箱");
const auto gateway=service->createCategory("网关");
QVERIFY(service->createProductType(safe.value().id,"F25",{}).ok());
QCOMPARE(service->createProductType(safe.value().id,"F25",{}).error().field,QString("typeCode"));
QVERIFY(service->createProductType(gateway.value().id,"F25",{}).ok());
```

- [ ] **Step 2: 运行 RED**

Run:

```bash
cmake --build build/Qt_6_10_0_MinGW_64_bit-Debug --target tst_master_data_service tst_inbound_service tst_logistics_service
ctest --test-dir build/Qt_6_10_0_MinGW_64_bit-Debug -R '^(master_data|inbound|logistics)$' --output-on-failure
```

Expected: 编译失败，因为 `ProductType`、无用途明细和新快照 API 尚未定义。

- [ ] **Step 3: 一次替换模型、Repository 与 Service**

删除 `Material`、`UsageType` 和所有用途关联 API；把文档持久化改为 `product_type_id` 和分类/类型快照；主数据唯一性查询同时绑定 `category_id`、`type_code` 和排除 ID。先确认目标 ID 和分类存在，再 trim 类型编号和说明，错误字段固定为 `categoryId` 或 `typeCode`。

入库确认读取 `productTypeById()` 和 `categoryById()`，要求两者启用并保存分类、类型、地点快照。物流确认删除用途读取及关联校验，从批次取得产品类型和分类；草稿校验用 `QSet<EntityId>` 拒绝重复批次，继续按“批次 + 来源地点”聚合校验库存。现有 `BEGIN IMMEDIATE`、库存记账、取消冲销和失败回滚边界不变。

- [ ] **Step 4: 增加业务回归断言**

覆盖：停用分类或类型不能确认新入库但历史按 ID 可读；同一草稿重复批次返回 `duplicate_batch` 和 `lines[1].batchId`；不同批次正常确认；主数据改名后已确认入库和物流仍返回原分类/类型快照；取消冲销后所有批次/库位余额非负；任一快照或流水写入失败不留下部分状态。

```cpp
value.lines={{batchId,40},{batchId,10}};
const auto rejected=logistics->createDraft(value);
QCOMPARE(rejected.error().code,QStringLiteral("duplicate_batch"));
QCOMPARE(rejected.error().field,QStringLiteral("lines[1].batchId"));
```

- [ ] **Step 5: 运行 GREEN**

Run: 同 Step 2。
Expected: master_data、inbound、logistics 全部 PASS，整个 `logistics_master_data` 库可编译。

- [ ] **Step 6: Commit（仅获授权后）**

```bash
git add src/core/models.h src/repositories/master_data_repository.* src/repositories/document_repository.* src/services/master_data_service.* src/services/inbound_service.* src/services/logistics_service.* tests/tst_master_data_service.cpp tests/tst_inbound_service.cpp tests/tst_logistics_service.cpp
git commit -m "refactor: replace materials and usages with product types"
```

### Task 3: 重建库存查询与报表数据集

**Files:**
- Modify: `src/services/inventory_query_service.h`
- Modify: `src/services/inventory_query_service.cpp`
- Modify: `tests/tst_inventory_queries.cpp`
- Modify: `tests/tst_report_service.cpp`

**Interfaces:**
- Produces:

```cpp
struct ProductTypeBalance { EntityId productTypeId; QString categoryName; QString typeCode; qint64 quantity; };
struct BatchBalance { EntityId batchId; EntityId productTypeId; QString batchNo; QString categoryName; QString typeCode; qint64 inboundQuantity; qint64 remainingQuantity; };
struct AvailableBatch { EntityId batchId; EntityId productTypeId; QString batchNo; QString categoryName; QString typeCode; qint64 quantity; };
struct LocationProductBalance { EntityId locationId; QString locationName; EntityId productTypeId; QString categoryName; QString typeCode; qint64 quantity; };
struct BatchLocationBalance { EntityId locationId; QString locationName; qint64 quantity; };
struct BatchTimelineItem { EntityId id; QString sourceDocument; QString direction; qint64 quantityDelta; QString locationName; QDateTime businessAt; QDateTime createdAt; QString batchNo; QString categoryName; QString typeCode; };
struct InboundReportRow { QString batchNo; QString categoryName; QString typeCode; qint64 quantity; QString locationName; QDateTime businessAt; };
struct OutboundReportRow { QString documentNo; QString batchNo; QString categoryName; QString typeCode; qint64 quantity; QString sourceName; QString destinationName; QDateTime businessAt; };
struct BusinessRecord { QString sourceDocument; QString businessType; QDateTime businessAt; QString sourceName; QString destinationName; qint64 quantity; };
struct DashboardSummary { qint64 totalInventory; qint64 companyInventory; qint64 factoryInventory; qint64 monthlyInbound; qint64 monthlyOutbound; QVector<BusinessRecord> recentRecords; };

Result<qint64> balance(EntityId batchId,EntityId locationId) const;
Result<qint64> batchRemaining(EntityId batchId) const;
Result<QVector<AvailableBatch>> availableBatches(EntityId locationId) const;
Result<QVector<ProductTypeBalance>> productTypeBalances() const;
Result<QVector<BatchBalance>> batchBalances() const;
Result<QVector<BatchBalance>> historicalBatches() const;
Result<QVector<LocationProductBalance>> locationProductBalances() const;
Result<QVector<BatchLocationBalance>> batchLocationBalances(EntityId batchId) const;
Result<QVector<BatchTimelineItem>> batchTimeline(EntityId batchId) const;
Result<QVector<InboundReportRow>> inboundReport(const DateRange &) const;
Result<QVector<OutboundReportRow>> outboundReport(const DateRange &) const;
Result<DashboardSummary> dashboardSummary(const QDate &month) const;
```

余额查询 `productTypeBalances()`、`batchBalances()`、`locationProductBalances()` 和 `availableBatches()` 以当前 `product_types` 与 `product_categories` 取得展示名称；`historicalBatches()`、`batchTimeline()`、`inboundReport()` 和 `outboundReport()` 只使用确认时快照。`historicalBatches()` 同时返回原始 `inboundQuantity` 与自然求和的 `remainingQuantity`，供追踪摘要展示。

- [ ] **Step 1: 写失败查询测试**

使用 820 件流程断言产品类型总库存 470、工厂 A 250、工厂 B 220；`historicalBatches()` 返回 `inboundQuantity=820` 和 `remainingQuantity=470`；出库报表仅含两张最终出库；时间线不含用途字段。

- [ ] **Step 2: 运行 RED**

Run:

```bash
cmake --build build/Qt_6_10_0_MinGW_64_bit-Debug --target tst_inventory_queries tst_report_service
ctest --test-dir build/Qt_6_10_0_MinGW_64_bit-Debug -R '^(inventory_queries|report_service)$' --output-on-failure
```

Expected: 编译失败，因为新查询 API 不存在。

- [ ] **Step 3: 实现聚合查询**

所有查询按上一接口约定选择当前主数据或确认快照；保留每个 `while(q.next())` 后的 `lastError()` 检查；删除 `usageShipmentSummary()`。

- [ ] **Step 4: 实现入库与出库明细查询**

日期区间使用 `[from, to + 1 day)`；出库仅选择 `type='Outbound' AND status='Confirmed'`；取消单不计入业务明细报表。

- [ ] **Step 5: 运行 GREEN**

Run: 同 Step 2。
Expected: 查询和 CSV 编码测试全部 PASS。

- [ ] **Step 6: Commit（仅获授权后）**

```bash
git add src/services/inventory_query_service.* tests/tst_inventory_queries.cpp tests/tst_report_service.cpp
git commit -m "feat: report inventory by product type"
```

### Task 4: 一次改造全部 UI 消费方

**Files:**
- Modify: `src/ui/dialogs/master_data_dialog.h`
- Modify: `src/ui/dialogs/master_data_dialog.cpp`
- Modify: `src/ui/pages/master_data_page.h`
- Modify: `src/ui/pages/master_data_page.cpp`
- Modify: `src/ui/dialogs/inbound_dialog.h`
- Modify: `src/ui/dialogs/inbound_dialog.cpp`
- Modify: `src/ui/dialogs/logistics_dialog.h`
- Modify: `src/ui/dialogs/logistics_dialog.cpp`
- Modify: `src/ui/pages/inventory_page.h`
- Modify: `src/ui/pages/inventory_page.cpp`
- Modify: `src/ui/pages/batch_tracking_page.cpp`
- Modify: `src/ui/pages/reports_page.cpp`
- Modify: `tests/tst_query_pages.cpp`
- Modify: `tests/tst_document_dialogs.cpp`

**Interfaces:**
- Consumes: Task 2 的 `ProductType`/无用途文档 API 和 Task 3 的完整查询 API。
- Produces: 三个基础资料页签；入库显示“分类 / 类型编号”；物流明细为“批次、产品类型、数量”；库存三个视图；无用途追踪；五类报表。

- [ ] **Step 1: 写失败 UI 测试**

`tests/tst_query_pages.cpp` 断言：基础资料正好三个页签；不存在 `usagesTable`、`relationUsages`；产品类型对话框只创建 `categoryId`、`typeCode`、`description`；库存页签和列标题正确；追踪摘要同时显示入库数量与当前剩余且时间线无用途；报表 selector 有五项，入库/出库 CSV 包含分类、类型、批次、数量、地点和业务时间。

`tests/tst_document_dialogs.cpp` 断言：物流表正好三列且不存在用途 combo；切换批次后产品类型正确；重复批次聚焦第二条批次；停用类型或所属分类只在历史只读模式可见，新建入库下拉框不包含它们。

- [ ] **Step 2: 运行 RED**

Run:

```bash
cmake --build build/Qt_6_10_0_MinGW_64_bit-Debug --target logistics_ui tst_query_pages tst_document_dialogs
```

Expected: 编译失败，因为所有 UI 消费方仍引用已删除的旧 API；这一步不运行旧测试二进制。

- [ ] **Step 3: 改造基础资料和业务对话框**

基础资料删除关系/用途页面，使用显式 `switch` 将当前页映射到 `MasterDataDialog::Entity { Category, ProductType, Location }`；产品类型只显示分类、类型编号、说明。入库成员改为 `productType_`/`productTypeId`；物流 `BatchChoice` 保存 `productTypeId`、`categoryName`、`typeCode`，移除 `loadUsages()`/`visibleUsageIds()`，产品类型列只读。

- [ ] **Step 4: 改造库存、追踪和报表页面**

库存页只创建 `productTypeBalances`、`locationProductBalances`、`batchBalances` 三张表并传播首个查询错误。追踪摘要显示“批次 / 分类 / 类型 / 入库数量 / 当前剩余”，时间线移除用途列。报表调用 Task 3 的五个数据集并继续用 `QSaveFile` 原子导出。

- [ ] **Step 5: 运行 GREEN**

Run:

```bash
cmake --build build/Qt_6_10_0_MinGW_64_bit-Debug --target logistics_ui tst_query_pages tst_document_dialogs
ctest --test-dir build/Qt_6_10_0_MinGW_64_bit-Debug -R '^(query_pages|document_dialogs)$' --output-on-failure
```

Expected: UI 库编译成功，query_pages 和 document_dialogs 全部 PASS。

- [ ] **Step 6: Commit（仅获授权后）**

```bash
git add src/ui/dialogs/master_data_dialog.* src/ui/pages/master_data_page.* src/ui/dialogs/inbound_dialog.* src/ui/dialogs/logistics_dialog.* src/ui/pages/inventory_page.* src/ui/pages/batch_tracking_page.cpp src/ui/pages/reports_page.cpp tests/tst_query_pages.cpp tests/tst_document_dialogs.cpp
git commit -m "feat: present inventory by product type"
```

### Task 5: 全应用文案、装配与端到端验收

**Files:**
- Modify: `src/app/application.cpp`
- Modify: `src/ui/pages/dashboard_page.cpp`
- Modify: `tests/tst_main_window.cpp`
- Modify: `tests/tst_end_to_end.cpp`
- Create: `tests/migration_smoke.cpp`
- Modify: `tests/CMakeLists.txt`
- Inspect: `CMakeLists.txt`（现有生产源文件列表无需变化）

**Interfaces:**
- Produces: 无“物料/用途/物料用途”用户可见文案；完整 v2 应用和安装目标。

- [ ] **Step 1: 写失败端到端测试**

建立“保险箱/F25”，入库 820，调拨 A 500、B 320，出库 A 250、B 100；断言公司 0、A 250、B 220、总库存 470。遍历主窗口可见控件文本，断言不含“物料”“型号”“用途”。

- [ ] **Step 2: 运行 RED**

Run:

```bash
cmake --build build/Qt_6_10_0_MinGW_64_bit-Debug --target tst_main_window tst_end_to_end
ctest --test-dir build/Qt_6_10_0_MinGW_64_bit-Debug -R '^(main_window|end_to_end)$' --output-on-failure
```

Expected: 旧夹具与旧文案导致失败。

- [ ] **Step 3: 更新装配和工作台文案**

确认 `Application` 只装配新 API 页面；删除所有残留 usage 调用；最近业务与指标保持现有计算语义。

- [ ] **Step 4: 搜索旧概念**

Run:

```bash
rg -ni "usage|material|model_code|modelcode|物料|用途|型号" src tests
```

Expected: 生产模型、服务和 UI 0 matches。允许旧名称仅出现在 `src/db/migrations/001_initial.sql`、`src/db/migrations/002_product_types.sql` 和 `tests/tst_database.cpp` 的 v1 迁移夹具中；逐条检查这些命中均有迁移上下文，不允许其他白名单。

- [ ] **Step 5: 运行完整 Debug 验证**

Run:

```bash
cmake --build build/Qt_6_10_0_MinGW_64_bit-Debug -j 2
QT_QPA_PLATFORM=minimal ctest --test-dir build/Qt_6_10_0_MinGW_64_bit-Debug --output-on-failure
```

Expected: 10/10 或更新后的全部测试 PASS。

- [ ] **Step 6: 执行 Release 安装验证**

Run:

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=D:/OSApplication/Work/Code/Qt6/6.10.0/mingw_64 -DBUILD_TESTING=ON
cmake --build build-release -j 2
QT_QPA_PLATFORM=minimal ctest --test-dir build-release --output-on-failure
cmake --install build-release --prefix dist
```

`CMAKE_PREFIX_PATH` 使用当前工作站已安装的 Qt 6.10 MinGW 路径；若 Qt 安装位置变化，先从现有 Debug build 的 `CMakeCache.txt` 读取实际路径再配置，不能猜测新路径。

Expected: 全部测试 PASS，`dist/LogisticsInventory.exe` 和 Qt 运行库存在。

- [ ] **Step 7: 自动化迁移副本冒烟**

新增 `tests/migration_smoke.cpp` 和 CMake 目标 `migration_smoke`，链接 `logistics_database` 与 `Qt6::Core`。该 CLI 接受以下参数：

```text
migration_smoke --seed-v1 <db-path>
migration_smoke --migrate <db-path>
migration_smoke --verify <db-path>
migration_smoke --print-app-data
```

`--seed-v1` 使用与 Task 1 相同的 v1 schema/夹具生成持久文件；`--migrate` 调用生产 `Database::open()`/`migrate()` 后立即退出；`--verify` 校验 version=2、批次/单据/流水数、聚合明细、冲销关系和逐库位余额；`--print-app-data` 输出 `QStandardPaths::AppLocalDataLocation`。全部模式都不创建 `QApplication`，不会进入 GUI 事件循环。

Run:

```bash
cmake --build build-release --target migration_smoke
SMOKE_ROOT="$(pwd)/build/migration-smoke/localappdata"
DB_PATH="$SMOKE_ROOT/LogisticsInventory/LogisticsInventory/logistics.sqlite"
ACTUAL_APP_DATA="$(LOCALAPPDATA="$SMOKE_ROOT" ./build-release/tests/migration_smoke --print-app-data)"
EXPECTED_APP_DATA="$(pwd)/build/migration-smoke/localappdata/LogisticsInventory/LogisticsInventory"
test "$(cygpath -m "$ACTUAL_APP_DATA")" = "$(cygpath -m "$EXPECTED_APP_DATA")" || exit 1
LOCALAPPDATA="$SMOKE_ROOT" ./build-release/tests/migration_smoke --seed-v1 "$DB_PATH"
LOCALAPPDATA="$SMOKE_ROOT" ./build-release/tests/migration_smoke --migrate "$DB_PATH"
LOCALAPPDATA="$SMOKE_ROOT" ./build-release/tests/migration_smoke --verify "$DB_PATH"
LOCALAPPDATA="$SMOKE_ROOT" ./build-release/tests/migration_smoke --migrate "$DB_PATH"
LOCALAPPDATA="$SMOKE_ROOT" ./build-release/tests/migration_smoke --verify "$DB_PATH"
```

`migration_smoke --print-app-data` 与 shell 都使用正斜杠规范化路径；如果环境没有 `cygpath`，由 helper 额外提供 `--assert-app-data-under <root>` 并用 `QDir::cleanPath`/`QFileInfo::canonicalFilePath` 验证。路径不相等必须退出，不执行 seed。第二次 migrate/verify 证明迁移幂等。整个流程不启动安装版 GUI，也不读取真实 `%LOCALAPPDATA%`；完成后仅清理 `build/migration-smoke`。

- [ ] **Step 8: 最终静态检查**

Run:

```bash
git diff --check
rg -n "QSql|SELECT|INSERT|UPDATE|DELETE|CREATE TABLE" src/ui
```

Expected: `diff --check` 无输出；UI SQL 搜索 0 matches。

- [ ] **Step 9: Commit（仅获授权后）**

```bash
git add src/app/application.cpp src/ui/pages/dashboard_page.cpp tests/tst_main_window.cpp tests/tst_end_to_end.cpp tests/migration_smoke.cpp tests/CMakeLists.txt
git commit -m "test: validate simplified product type workflow"
```

## Completion Criteria

- v1 数据库自动升级到 version 2，失败时完整回滚。
- 产品分类/类型、批次、单据、流水、地点和库存总量迁移后保持一致。
- 源码和用户界面不再包含用途业务路径。
- 新入库只选择启用的“分类 / 类型编号”。
- 物流明细只录入批次和数量，并拒绝同单重复批次。
- 库存、追踪、工作台和五类报表使用分类/类型语义。
- Debug、Release 全量测试通过，安装目录可独立启动。
- 820 件流程最终为公司仓 0、工厂 A 250、工厂 B 220、总库存 470。
