CREATE TEMP TABLE migration3_audit(
 document_count INTEGER NOT NULL,
 line_count INTEGER NOT NULL,
 line_quantity INTEGER NOT NULL
);
INSERT INTO migration3_audit
SELECT (SELECT COUNT(*) FROM logistics_documents),
       (SELECT COUNT(*) FROM logistics_document_lines),
       (SELECT COALESCE(SUM(quantity),0) FROM logistics_document_lines);

CREATE TABLE product_models(
 id INTEGER PRIMARY KEY,
 product_type_id INTEGER NOT NULL REFERENCES product_types(id),
 model_code TEXT NOT NULL,
 description TEXT NOT NULL DEFAULT '',
 enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0,1)),
 UNIQUE(product_type_id,model_code)
);

CREATE TABLE logistics_documents_v3(
 id INTEGER PRIMARY KEY,
 document_no TEXT UNIQUE,
 type TEXT NOT NULL CHECK(type IN ('Transfer','Outbound','Return')),
 source_location_id INTEGER NOT NULL REFERENCES locations(id),
 destination_location_id INTEGER NOT NULL REFERENCES locations(id),
 business_at TEXT NOT NULL,
 status TEXT NOT NULL DEFAULT 'Draft' CHECK(status IN ('Draft','Confirmed','Cancelled')),
 note TEXT NOT NULL DEFAULT '',
 source_name_snapshot TEXT,
 destination_name_snapshot TEXT,
 created_at TEXT NOT NULL,
 confirmed_at TEXT,
 cancelled_at TEXT,
 CHECK(source_location_id <> destination_location_id)
);
INSERT INTO logistics_documents_v3
SELECT id,document_no,type,source_location_id,destination_location_id,business_at,
       status,note,source_name_snapshot,destination_name_snapshot,
       created_at,confirmed_at,cancelled_at
FROM logistics_documents;

CREATE TABLE logistics_document_lines_v3(
 id INTEGER PRIMARY KEY,
 document_id INTEGER NOT NULL REFERENCES logistics_documents_v3(id) ON DELETE CASCADE,
 batch_id INTEGER NOT NULL REFERENCES inbound_batches(id),
 product_model_id INTEGER REFERENCES product_models(id),
 quantity INTEGER NOT NULL CHECK(quantity > 0),
 batch_no_snapshot TEXT,
 category_name_snapshot TEXT,
 type_code_snapshot TEXT,
 model_code_snapshot TEXT
);
INSERT INTO logistics_document_lines_v3(
 id,document_id,batch_id,product_model_id,quantity,
 batch_no_snapshot,category_name_snapshot,type_code_snapshot,model_code_snapshot)
SELECT id,document_id,batch_id,NULL,quantity,
       batch_no_snapshot,category_name_snapshot,type_code_snapshot,NULL
FROM logistics_document_lines;

CREATE TEMP TABLE migration3_assert(ok INTEGER NOT NULL CHECK(ok=1));
INSERT INTO migration3_assert
SELECT COUNT(*)=(SELECT document_count FROM migration3_audit) FROM logistics_documents_v3;
INSERT INTO migration3_assert
SELECT COUNT(*)=(SELECT line_count FROM migration3_audit) FROM logistics_document_lines_v3;
INSERT INTO migration3_assert
SELECT COALESCE(SUM(quantity),0)=(SELECT line_quantity FROM migration3_audit)
FROM logistics_document_lines_v3;

DROP TABLE logistics_document_lines;
DROP TABLE logistics_documents;
ALTER TABLE logistics_documents_v3 RENAME TO logistics_documents;
ALTER TABLE logistics_document_lines_v3 RENAME TO logistics_document_lines;

CREATE UNIQUE INDEX idx_lines_document_batch_model
 ON logistics_document_lines(document_id,batch_id,COALESCE(product_model_id,0));
CREATE INDEX idx_lines_model ON logistics_document_lines(product_model_id);

DROP TABLE migration3_assert;
DROP TABLE migration3_audit;
