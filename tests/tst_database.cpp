#include "test_support.h"

#include <QMap>
#include <QSqlQuery>
#include <QtTest>

namespace {
using NumericSnapshot = QMap<QString, qlonglong>;
using TextSnapshot = QMap<QString, QString>;

NumericSnapshot balanceSnapshot(TestDatabase &db)
{
    NumericSnapshot result;
    QSqlQuery query(db.database().connection());
    if (!query.exec(QStringLiteral(
            "SELECT batch_id,location_id,SUM(quantity_delta) FROM inventory_entries "
            "GROUP BY batch_id,location_id ORDER BY batch_id,location_id")))
        return {};
    while (query.next()) {
        result.insert(QStringLiteral("%1/%2").arg(query.value(0).toString(), query.value(1).toString()),
                      query.value(2).toLongLong());
    }
    return result;
}

NumericSnapshot documentQuantitySnapshot(TestDatabase &db)
{
    NumericSnapshot result;
    QSqlQuery query(db.database().connection());
    if (!query.exec(QStringLiteral(
            "SELECT document_id,SUM(quantity) FROM logistics_document_lines "
            "GROUP BY document_id ORDER BY document_id")))
        return {};
    while (query.next())
        result.insert(query.value(0).toString(), query.value(1).toLongLong());
    return result;
}

NumericSnapshot tableRowCountSnapshot(TestDatabase &db, const QStringList &tables)
{
    NumericSnapshot result;
    for (const QString &table : tables)
        result.insert(table, db.scalar(QStringLiteral("SELECT COUNT(*) FROM %1").arg(table)).toLongLong());
    return result;
}

TextSnapshot tableSchemaSnapshot(TestDatabase &db)
{
    TextSnapshot result;
    QSqlQuery query(db.database().connection());
    if (!query.exec(QStringLiteral(
            "SELECT name,sql FROM sqlite_master WHERE type='table' "
            "AND name NOT LIKE 'sqlite_%' ORDER BY name")))
        return {};
    while (query.next())
        result.insert(query.value(0).toString(), query.value(1).toString());
    return result;
}

TextSnapshot inventoryEntrySnapshot(TestDatabase &db)
{
    TextSnapshot result;
    QSqlQuery query(db.database().connection());
    if (!query.exec(QStringLiteral(
            "SELECT id,printf('%d|%d|%s|%d|%s|%d|%s|%s|%s',batch_id,location_id,source_type,"
            "source_id,COALESCE(source_line_id,''),quantity_delta,business_at,created_at,"
            "COALESCE(reversal_of_entry_id,'')) FROM inventory_entries ORDER BY id")))
        return {};
    while (query.next())
        result.insert(query.value(0).toString(), query.value(1).toString());
    return result;
}

bool loadV1Fixture(TestDatabase &db)
{
    if (!db.open()
        || !db.executeResource(QStringLiteral(":/migrations/001_initial.sql"))) {
        return false;
    }
    const QStringList statements{
        "CREATE TABLE schema_migrations(version INTEGER PRIMARY KEY)",
        "INSERT INTO schema_migrations(version) VALUES(1)",
        "INSERT INTO product_categories(id,name) VALUES(1,'保险箱分类')",
        "INSERT INTO materials(id,category_id,name,model_code,specification) VALUES(1,1,'旧保险箱物料','F25','说明')",
        "INSERT INTO usage_types(id,code,name) VALUES(1,'OLD-A','旧用途甲')",
        "INSERT INTO usage_types(id,code,name) VALUES(2,'OLD-B','旧用途乙')",
        "INSERT INTO material_usage_types(material_id,usage_type_id) VALUES(1,1)",
        "INSERT INTO material_usage_types(material_id,usage_type_id) VALUES(1,2)",
        "INSERT INTO locations(id,name,type,counts_as_inventory) VALUES(1,'公司仓','Company',1)",
        "INSERT INTO locations(id,name,type,counts_as_inventory) VALUES(2,'工厂仓','Factory',1)",
        "INSERT INTO locations(id,name,type,counts_as_inventory) VALUES(3,'项目仓','ProjectSite',1)",
        "INSERT INTO inbound_batches(id,batch_no,material_id,quantity,initial_location_id,business_at,status,material_name_snapshot,model_code_snapshot,location_name_snapshot,created_at,confirmed_at) VALUES(1,'B-1',1,100,1,'2026-09-01','Confirmed','旧保险箱物料','F25','公司仓','2026-09-01','2026-09-01')",
        "INSERT INTO inbound_batches(id,batch_no,material_id,quantity,initial_location_id,business_at,status,material_name_snapshot,model_code_snapshot,location_name_snapshot,created_at,confirmed_at) VALUES(2,'B-2',1,75,2,'2026-09-03','Confirmed','旧保险箱物料','F25','工厂仓','2026-09-03','2026-09-03')",
        "INSERT INTO logistics_documents(id,document_no,type,source_location_id,destination_location_id,business_at,status,created_at,confirmed_at) VALUES(1,'L-1','Transfer',1,2,'2026-09-02','Confirmed','2026-09-02','2026-09-02')",
        "INSERT INTO logistics_documents(id,document_no,type,source_location_id,destination_location_id,business_at,status,created_at,confirmed_at) VALUES(2,'L-2','Transfer',2,3,'2026-09-04','Confirmed','2026-09-04','2026-09-04')",
        "INSERT INTO logistics_document_lines(id,document_id,batch_id,usage_type_id,quantity,batch_no_snapshot,material_name_snapshot,model_code_snapshot) VALUES(1,1,1,1,40,'B-1','旧保险箱物料','F25')",
        "INSERT INTO logistics_document_lines(id,document_id,batch_id,usage_type_id,quantity,batch_no_snapshot,material_name_snapshot,model_code_snapshot) VALUES(2,1,1,2,60,'B-1','旧保险箱物料','F25')",
        "INSERT INTO logistics_document_lines(id,document_id,batch_id,usage_type_id,quantity,batch_no_snapshot,material_name_snapshot,model_code_snapshot) VALUES(3,2,2,1,25,'B-2','旧保险箱物料','F25')",
        "INSERT INTO inventory_entries(id,batch_id,location_id,source_type,source_id,quantity_delta,business_at,created_at) VALUES(1,1,1,'Inbound',1,100,'2026-09-01','2026-09-01')",
        "INSERT INTO inventory_entries(id,batch_id,location_id,source_type,source_id,source_line_id,quantity_delta,business_at,created_at) VALUES(2,1,1,'Logistics',1,1,-40,'2026-09-02','2026-09-02')",
        "INSERT INTO inventory_entries(id,batch_id,location_id,source_type,source_id,source_line_id,quantity_delta,business_at,created_at) VALUES(3,1,1,'Logistics',1,2,-60,'2026-09-02','2026-09-02')",
        "INSERT INTO inventory_entries(id,batch_id,location_id,source_type,source_id,source_line_id,quantity_delta,business_at,created_at) VALUES(4,1,1,'Logistics',1,2,-10,'2026-09-03','2026-09-03')",
        "INSERT INTO inventory_entries(id,batch_id,location_id,source_type,source_id,source_line_id,quantity_delta,business_at,created_at,reversal_of_entry_id) VALUES(5,1,1,'Logistics',1,2,10,'2026-09-03','2026-09-03',4)",
        "INSERT INTO inventory_entries(id,batch_id,location_id,source_type,source_id,quantity_delta,business_at,created_at) VALUES(6,2,2,'Inbound',2,75,'2026-09-03','2026-09-03')",
        "INSERT INTO inventory_entries(id,batch_id,location_id,source_type,source_id,source_line_id,quantity_delta,business_at,created_at) VALUES(7,2,2,'Logistics',2,3,-25,'2026-09-04','2026-09-04')",
        "INSERT INTO inventory_entries(id,batch_id,location_id,source_type,source_id,source_line_id,quantity_delta,business_at,created_at) VALUES(8,2,3,'Logistics',2,3,25,'2026-09-04','2026-09-04')"
    };
    for (const QString &statement : statements) {
        if (!db.execute(statement))
            return false;
    }
    return true;
}
}

class DatabaseTest : public QObject {
    Q_OBJECT

private slots:
    void migratesEmptyDatabase();
    void createsCoreTables();
    void commitsSuccessfulTransaction();
    void rollsBackFailedTransaction();
    void enforcesCheckConstraints();
    void rejectsNonPositiveInboundQuantity();
    void rejectsNonPositiveLogisticsLineQuantity();
    void logisticsSchemaSupportsSnapshotsAndNullDraftNumbers();
    void inboundSchemaSupportsNullDraftNumbers();
    void rejectsZeroInventoryDelta();
    void enforcesUniqueConstraints();
    void migratesV1DataToProductTypes();
    void migrationFailureRollsBackV1();
};

void DatabaseTest::migratesEmptyDatabase()
{
    TestDatabase db;
    QVERIFY(db.openAndMigrate());
    QCOMPARE(db.scalar("SELECT MAX(version) FROM schema_migrations"), QVariant(2));
    QCOMPARE(db.scalar("PRAGMA foreign_keys"), QVariant(1));
    QVERIFY(db.tableExists("inventory_entries"));
}

void DatabaseTest::createsCoreTables()
{
    TestDatabase db;
    QVERIFY(db.openAndMigrate());
    const QStringList tables = {
        "product_categories", "product_types", "locations", "inbound_batches",
        "logistics_documents", "logistics_document_lines", "inventory_entries"
    };
    for (const QString &table : tables)
        QVERIFY2(db.tableExists(table), qPrintable(table));
    for (const QString &table : {"materials", "usage_types", "material_usage_types"})
        QVERIFY2(!db.tableExists(table), qPrintable(table));
}

void DatabaseTest::commitsSuccessfulTransaction()
{
    TestDatabase db;
    QVERIFY(db.openAndMigrate());
    QString error;
    QVERIFY(db.database().transaction(Database::TransactionMode::Immediate,
                                      [&db]() {
                                          return db.execute("INSERT INTO product_categories(name) VALUES('Steel')");
                                      },
                                      &error));
    QCOMPARE(db.scalar("SELECT COUNT(*) FROM product_categories WHERE name = 'Steel'"), QVariant(1));
}

void DatabaseTest::rollsBackFailedTransaction()
{
    TestDatabase db;
    QVERIFY(db.openAndMigrate());
    QString error;
    QVERIFY(!db.database().transaction(Database::TransactionMode::Immediate,
                                       [&db]() {
                                           return db.execute("INSERT INTO product_categories(name) VALUES('Steel')")
                                               && false;
                                       },
                                       &error));
    QCOMPARE(db.scalar("SELECT COUNT(*) FROM product_categories WHERE name = 'Steel'"), QVariant(0));
}

void DatabaseTest::enforcesCheckConstraints()
{
    TestDatabase db;
    QVERIFY(db.openAndMigrate());
    QVERIFY(!db.execute("INSERT INTO product_categories(name, enabled) VALUES('Invalid', 2)"));
    QVERIFY(!db.execute("INSERT INTO locations(name, type, counts_as_inventory) VALUES('X', 'Invalid', 1)"));
}

void DatabaseTest::rejectsNonPositiveInboundQuantity()
{
    TestDatabase db;
    QVERIFY(db.openAndMigrate());
    QVERIFY(db.execute("INSERT INTO product_categories(id, name) VALUES(1, 'Steel')"));
    QVERIFY(db.execute("INSERT INTO product_types(id, category_id, type_code, description) VALUES(1, 1, 'P-1', 'Plate')"));
    QVERIFY(db.execute("INSERT INTO locations(id, name, type, counts_as_inventory) VALUES(1, 'Warehouse', 'Company', 1)"));

    for (const int quantity : {0, -1}) {
        const QString sql = QStringLiteral(
            "INSERT INTO inbound_batches(batch_no, product_type_id, quantity, initial_location_id, business_at, created_at) "
            "VALUES('B-%1', 1, %1, 1, '2026-09-10', '2026-09-10')")
                                .arg(quantity);
        QVERIFY2(!db.execute(sql), qPrintable(sql));
    }
}

void DatabaseTest::rejectsNonPositiveLogisticsLineQuantity()
{
    TestDatabase db;
    QVERIFY(db.openAndMigrate());
    QVERIFY(db.execute("INSERT INTO product_categories(id, name) VALUES(1, 'Steel')"));
    QVERIFY(db.execute("INSERT INTO product_types(id, category_id, type_code, description) VALUES(1, 1, 'P-1', 'Plate')"));
    QVERIFY(db.execute("INSERT INTO locations(id, name, type, counts_as_inventory) VALUES(1, 'Source', 'Company', 1)"));
    QVERIFY(db.execute("INSERT INTO locations(id, name, type, counts_as_inventory) VALUES(2, 'Destination', 'Factory', 1)"));
    QVERIFY(db.execute("INSERT INTO inbound_batches(id, batch_no, product_type_id, quantity, initial_location_id, business_at, created_at) "
                       "VALUES(1, 'B-1', 1, 1, 1, '2026-09-10', '2026-09-10')"));
    QVERIFY(db.execute("INSERT INTO logistics_documents(id, document_no, type, source_location_id, destination_location_id, business_at, created_at) "
                       "VALUES(1, 'L-1', 'Transfer', 1, 2, '2026-09-10', '2026-09-10')"));

    for (const int quantity : {0, -1}) {
        const QString sql = QStringLiteral(
            "INSERT INTO logistics_document_lines(document_id, batch_id, quantity) "
            "VALUES(1, 1, %1)")
                                .arg(quantity);
        QVERIFY2(!db.execute(sql), qPrintable(sql));
    }
}

void DatabaseTest::logisticsSchemaSupportsSnapshotsAndNullDraftNumbers()
{
    TestDatabase db;
    QVERIFY(db.openAndMigrate());
    QCOMPARE(db.scalar(QStringLiteral(
                 "SELECT [notnull] FROM pragma_table_info('logistics_documents') "
                 "WHERE name='document_no'")),
             QVariant(0));
    QCOMPARE(db.scalar(QStringLiteral(
                 "SELECT COUNT(*) FROM pragma_table_info('logistics_document_lines') "
                 "WHERE name IN ('batch_no_snapshot','category_name_snapshot','type_code_snapshot')")),
             QVariant(3));

    QVERIFY(db.execute("INSERT INTO product_categories(id, name) VALUES(1, 'Steel')"));
    QVERIFY(db.execute("INSERT INTO product_types(id, category_id, type_code, description) "
                       "VALUES(1, 1, 'P-1', 'Plate')"));
    QVERIFY(db.execute("INSERT INTO locations(id, name, type, counts_as_inventory) "
                       "VALUES(1, 'Source', 'Company', 1)"));
    QVERIFY(db.execute("INSERT INTO locations(id, name, type, counts_as_inventory) "
                       "VALUES(2, 'Destination', 'Factory', 1)"));
    QVERIFY(db.execute("INSERT INTO inbound_batches(id, batch_no, product_type_id, quantity, "
                       "initial_location_id, business_at, created_at) "
                       "VALUES(1, 'B-1', 1, 1, 1, '2026-09-10', '2026-09-10')"));
    for (const int id : {1, 2}) {
        const QString sql = QStringLiteral(
            "INSERT INTO logistics_documents(id, document_no, type, source_location_id, "
            "destination_location_id, business_at, created_at) "
            "VALUES(%1, NULL, 'Transfer', 1, 2, '2026-09-10', '2026-09-10')").arg(id);
        QVERIFY2(db.execute(sql), qPrintable(sql));
    }
    QCOMPARE(db.scalar("SELECT COUNT(*) FROM logistics_documents WHERE document_no IS NULL"),
             QVariant(2));
    QVERIFY(db.execute(
        "UPDATE logistics_documents SET document_no='L-UNIQUE' WHERE id=1"));
    QVERIFY(!db.execute(
        "UPDATE logistics_documents SET document_no='L-UNIQUE' WHERE id=2"));
}

void DatabaseTest::inboundSchemaSupportsNullDraftNumbers()
{
    TestDatabase db;
    QVERIFY(db.openAndMigrate());
    QCOMPARE(db.scalar(QStringLiteral(
                 "SELECT [notnull] FROM pragma_table_info('inbound_batches') "
                 "WHERE name='batch_no'")),
             QVariant(0));

    QVERIFY(db.execute("INSERT INTO product_categories(id, name) VALUES(1, 'Steel')"));
    QVERIFY(db.execute("INSERT INTO product_types(id, category_id, type_code, description) "
                       "VALUES(1, 1, 'P-1', 'Plate')"));
    QVERIFY(db.execute("INSERT INTO locations(id, name, type, counts_as_inventory) "
                       "VALUES(1, 'Warehouse', 'Company', 1)"));
    for (const int id : {1, 2}) {
        const QString sql = QStringLiteral(
            "INSERT INTO inbound_batches(id, batch_no, product_type_id, quantity, "
            "initial_location_id, business_at, created_at) "
            "VALUES(%1, NULL, 1, 1, 1, '2026-09-10', '2026-09-10')").arg(id);
        QVERIFY2(db.execute(sql), qPrintable(sql));
    }
    QCOMPARE(db.scalar("SELECT COUNT(*) FROM inbound_batches WHERE batch_no IS NULL"),
             QVariant(2));
    QVERIFY(db.execute("UPDATE inbound_batches SET batch_no='B-UNIQUE' WHERE id=1"));
    QVERIFY(!db.execute("UPDATE inbound_batches SET batch_no='B-UNIQUE' WHERE id=2"));
}

void DatabaseTest::rejectsZeroInventoryDelta()
{
    TestDatabase db;
    QVERIFY(db.openAndMigrate());
    QVERIFY(db.execute("INSERT INTO product_categories(id, name) VALUES(1, 'Steel')"));
    QVERIFY(db.execute("INSERT INTO product_types(id, category_id, type_code, description) VALUES(1, 1, 'P-1', 'Plate')"));
    QVERIFY(db.execute("INSERT INTO locations(id, name, type, counts_as_inventory) VALUES(1, 'Warehouse', 'Company', 1)"));
    QVERIFY(db.execute("INSERT INTO inbound_batches(id, batch_no, product_type_id, quantity, initial_location_id, business_at, created_at) "
                       "VALUES(1, 'B-1', 1, 1, 1, '2026-09-10', '2026-09-10')"));
    QVERIFY(!db.execute("INSERT INTO inventory_entries(batch_id, location_id, source_type, source_id, quantity_delta, business_at, created_at) "
                        "VALUES(1, 1, 'Inbound', 1, 0, '2026-09-10', '2026-09-10')"));
}

void DatabaseTest::enforcesUniqueConstraints()
{
    TestDatabase db;
    QVERIFY(db.openAndMigrate());
    QVERIFY(db.execute("INSERT INTO product_categories(id, name) VALUES(1, 'Steel')"));
    QVERIFY(db.execute("INSERT INTO product_types(id, category_id, type_code) VALUES(1, 1, 'P-1')"));
    QVERIFY(!db.execute("INSERT INTO product_types(id, category_id, type_code) VALUES(2, 1, 'P-1')"));
}

void DatabaseTest::migratesV1DataToProductTypes()
{
    TestDatabase db;
    QVERIFY(loadV1Fixture(db));
    const QVariant oldEntryCount = db.scalar("SELECT COUNT(*) FROM inventory_entries");
    const NumericSnapshot oldBalances = balanceSnapshot(db);
    const NumericSnapshot oldDocumentQuantities = documentQuantitySnapshot(db);

    QVERIFY(db.migrate());
    QCOMPARE(db.scalar("SELECT MAX(version) FROM schema_migrations"), QVariant(2));
    QCOMPARE(db.scalar("SELECT type_code FROM product_types WHERE id=1"), QVariant("F25"));
    QCOMPARE(db.scalar("SELECT category_name_snapshot FROM inbound_batches WHERE id=1"),
             QVariant("保险箱分类"));
    QCOMPARE(db.scalar("SELECT category_name_snapshot FROM logistics_document_lines WHERE id=1"),
             QVariant("保险箱分类"));
    QCOMPARE(db.scalar("SELECT COUNT(*) FROM logistics_document_lines WHERE document_id=1"), QVariant(1));
    QCOMPARE(db.scalar("SELECT quantity FROM logistics_document_lines WHERE document_id=1"), QVariant(100));
    QCOMPARE(db.scalar("SELECT COUNT(*) FROM logistics_document_lines WHERE document_id=2"), QVariant(1));
    QCOMPARE(db.scalar("SELECT quantity FROM logistics_document_lines WHERE document_id=2"), QVariant(25));
    QCOMPARE(db.scalar("SELECT COUNT(*) FROM inventory_entries"), oldEntryCount);
    QCOMPARE(balanceSnapshot(db), oldBalances);
    QCOMPARE(documentQuantitySnapshot(db), oldDocumentQuantities);
    QCOMPARE(db.scalar("SELECT COUNT(*) FROM inventory_entries WHERE id IN (2,3,4,5) AND source_line_id=1"), QVariant(4));
    QCOMPARE(db.scalar("SELECT COUNT(*) FROM inventory_entries WHERE id IN (7,8) AND source_line_id=3"), QVariant(2));
    QCOMPARE(db.scalar("SELECT reversal_of_entry_id FROM inventory_entries WHERE id=5"), QVariant(4));
    QCOMPARE(db.scalar("SELECT COUNT(*) FROM pragma_foreign_key_check"), QVariant(0));
    QVERIFY(!db.tableExists("materials"));
    QVERIFY(!db.tableExists("usage_types"));
    QVERIFY(!db.tableExists("material_usage_types"));
}

void DatabaseTest::migrationFailureRollsBackV1()
{
    TestDatabase db;
    QVERIFY(loadV1Fixture(db));
    const QStringList v1Tables{
        "schema_migrations", "product_categories", "materials", "usage_types",
        "material_usage_types", "locations", "inbound_batches", "logistics_documents",
        "logistics_document_lines", "inventory_entries"
    };
    const TextSnapshot oldSchemas = tableSchemaSnapshot(db);
    const NumericSnapshot oldRowCounts = tableRowCountSnapshot(db, v1Tables);
    const NumericSnapshot oldBalances = balanceSnapshot(db);
    const NumericSnapshot oldDocumentQuantities = documentQuantitySnapshot(db);
    const TextSnapshot oldInventoryEntries = inventoryEntrySnapshot(db);
    QVERIFY(db.execute("CREATE TEMP TABLE line_id_map(existing INTEGER)"));

    QVERIFY(!db.migrate());
    QCOMPARE(db.scalar("SELECT MAX(version) FROM schema_migrations"), QVariant(1));
    QVERIFY(!db.tableExists("product_types"));
    QVERIFY(!db.tableExists("inbound_batches_v2"));
    QVERIFY(!db.tableExists("logistics_document_lines_v2"));
    QVERIFY(!db.tableExists("inventory_entries_v2"));
    QCOMPARE(tableSchemaSnapshot(db), oldSchemas);
    QCOMPARE(tableRowCountSnapshot(db, v1Tables), oldRowCounts);
    QCOMPARE(balanceSnapshot(db), oldBalances);
    QCOMPARE(documentQuantitySnapshot(db), oldDocumentQuantities);
    QCOMPARE(inventoryEntrySnapshot(db), oldInventoryEntries);
    QCOMPARE(db.scalar("SELECT COUNT(*) FROM locations"), QVariant(3));
}

QTEST_MAIN(DatabaseTest)
#include "tst_database.moc"
