CREATE TEMP TABLE migration_audit(
 batch_count INTEGER NOT NULL,
 document_count INTEGER NOT NULL,
 entry_count INTEGER NOT NULL
);
INSERT INTO migration_audit
SELECT (SELECT COUNT(*) FROM inbound_batches),
       (SELECT COUNT(*) FROM logistics_documents),
       (SELECT COUNT(*) FROM inventory_entries);

CREATE TEMP TABLE migration_balance_audit AS
SELECT batch_id,location_id,SUM(quantity_delta) AS balance
FROM inventory_entries GROUP BY batch_id,location_id;
CREATE TEMP TABLE migration_document_audit AS
SELECT document_id,SUM(quantity) AS quantity
FROM logistics_document_lines GROUP BY document_id;

CREATE TABLE product_types(
 id INTEGER PRIMARY KEY,
 category_id INTEGER NOT NULL REFERENCES product_categories(id),
 type_code TEXT NOT NULL,
 description TEXT NOT NULL DEFAULT '',
 enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0,1)),
 UNIQUE(category_id,type_code)
);
INSERT INTO product_types(id,category_id,type_code,description,enabled)
SELECT id,category_id,model_code,specification,enabled FROM materials;

CREATE TABLE inbound_batches_v2(
 id INTEGER PRIMARY KEY,
 batch_no TEXT UNIQUE,
 product_type_id INTEGER NOT NULL REFERENCES product_types(id),
 quantity INTEGER NOT NULL CHECK(quantity > 0),
 initial_location_id INTEGER NOT NULL REFERENCES locations(id),
 business_at TEXT NOT NULL,
 status TEXT NOT NULL DEFAULT 'Confirmed' CHECK(status IN ('Confirmed','Cancelled')),
 note TEXT NOT NULL DEFAULT '',
 category_name_snapshot TEXT,
 type_code_snapshot TEXT,
 location_name_snapshot TEXT,
 created_at TEXT NOT NULL,
 confirmed_at TEXT,
 cancelled_at TEXT
);
INSERT INTO inbound_batches_v2
SELECT b.id,b.batch_no,b.material_id,b.quantity,b.initial_location_id,b.business_at,b.status,b.note,
       c.name,
       COALESCE(b.model_code_snapshot,m.model_code),
       COALESCE(b.location_name_snapshot,l.name),
       b.created_at,b.confirmed_at,b.cancelled_at
FROM inbound_batches b
JOIN materials m ON m.id=b.material_id
JOIN product_categories c ON c.id=m.category_id
JOIN locations l ON l.id=b.initial_location_id;

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
INSERT INTO logistics_document_lines_v2
SELECT MIN(line.id),line.document_id,line.batch_id,SUM(line.quantity),
       COALESCE(MAX(line.batch_no_snapshot),MAX(batch.batch_no)),
       COALESCE(MAX(migrated_batch.category_name_snapshot),MAX(category.name)),
       COALESCE(MAX(line.model_code_snapshot),MAX(material.model_code))
FROM logistics_document_lines line
JOIN inbound_batches batch ON batch.id=line.batch_id
JOIN inbound_batches_v2 migrated_batch ON migrated_batch.id=line.batch_id
JOIN materials material ON material.id=batch.material_id
JOIN product_categories category ON category.id=material.category_id
GROUP BY line.document_id,line.batch_id;

CREATE TEMP TABLE line_id_map AS
SELECT old.id AS old_id, grouped.new_id
FROM logistics_document_lines old
JOIN (
  SELECT document_id,batch_id,MIN(id) AS new_id
  FROM logistics_document_lines
  GROUP BY document_id,batch_id
) grouped USING(document_id,batch_id);

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
INSERT INTO inventory_entries_v2
SELECT entry.id,entry.batch_id,entry.location_id,entry.source_type,entry.source_id,
       CASE WHEN entry.source_line_id IS NULL THEN NULL ELSE map.new_id END,
       entry.quantity_delta,entry.business_at,entry.created_at,entry.reversal_of_entry_id
FROM inventory_entries entry
LEFT JOIN line_id_map map ON map.old_id=entry.source_line_id;

CREATE TEMP TABLE migration_assert(ok INTEGER NOT NULL CHECK(ok=1));
INSERT INTO migration_assert SELECT COUNT(*)=(SELECT batch_count FROM migration_audit) FROM inbound_batches_v2;
INSERT INTO migration_assert SELECT COUNT(*)=(SELECT document_count FROM migration_audit) FROM logistics_documents;
INSERT INTO migration_assert SELECT COUNT(*)=(SELECT entry_count FROM migration_audit) FROM inventory_entries_v2;
INSERT INTO migration_assert
SELECT NOT EXISTS(
 SELECT 1 FROM migration_balance_audit old
 LEFT JOIN (SELECT batch_id,location_id,SUM(quantity_delta) balance FROM inventory_entries_v2 GROUP BY batch_id,location_id) current
 USING(batch_id,location_id) WHERE current.balance IS NOT old.balance
);
INSERT INTO migration_assert
SELECT NOT EXISTS(
 SELECT 1 FROM migration_document_audit old
 LEFT JOIN (SELECT document_id,SUM(quantity) quantity FROM logistics_document_lines_v2 GROUP BY document_id) current
 USING(document_id) WHERE current.quantity IS NOT old.quantity
);

DROP TABLE inventory_entries;
DROP TABLE logistics_document_lines;
DROP TABLE inbound_batches;
DROP TABLE material_usage_types;
DROP TABLE usage_types;
DROP TABLE materials;
ALTER TABLE inbound_batches_v2 RENAME TO inbound_batches;
ALTER TABLE logistics_document_lines_v2 RENAME TO logistics_document_lines;
ALTER TABLE inventory_entries_v2 RENAME TO inventory_entries;
CREATE INDEX idx_inventory_batch_location ON inventory_entries(batch_id, location_id);
CREATE INDEX idx_inventory_source ON inventory_entries(source_type, source_id);
CREATE INDEX idx_inventory_business_at ON inventory_entries(business_at);
CREATE INDEX idx_inventory_reversal ON inventory_entries(reversal_of_entry_id);

DROP TABLE line_id_map;
DROP TABLE migration_assert;
DROP TABLE migration_document_audit;
DROP TABLE migration_balance_audit;
DROP TABLE migration_audit;
