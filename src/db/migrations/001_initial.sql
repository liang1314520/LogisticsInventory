CREATE TABLE product_categories(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0,1)));
CREATE TABLE materials(id INTEGER PRIMARY KEY, category_id INTEGER NOT NULL REFERENCES product_categories(id), name TEXT NOT NULL, model_code TEXT NOT NULL UNIQUE, specification TEXT NOT NULL DEFAULT '', enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0,1)));
CREATE TABLE usage_types(id INTEGER PRIMARY KEY, code TEXT NOT NULL UNIQUE, name TEXT NOT NULL, description TEXT NOT NULL DEFAULT '', enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0,1)));
CREATE TABLE material_usage_types(material_id INTEGER NOT NULL REFERENCES materials(id), usage_type_id INTEGER NOT NULL REFERENCES usage_types(id), PRIMARY KEY(material_id, usage_type_id));
CREATE TABLE locations(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, type TEXT NOT NULL CHECK(type IN ('Company','Factory','Customer','ProjectSite','Other')), counts_as_inventory INTEGER NOT NULL CHECK(counts_as_inventory IN (0,1)), enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0,1)));

CREATE TABLE inbound_batches(
 id INTEGER PRIMARY KEY, batch_no TEXT UNIQUE, material_id INTEGER NOT NULL REFERENCES materials(id),
 quantity INTEGER NOT NULL CHECK(quantity > 0), initial_location_id INTEGER NOT NULL REFERENCES locations(id),
 business_at TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'Draft' CHECK(status IN ('Draft','Confirmed','Cancelled')),
 note TEXT NOT NULL DEFAULT '', material_name_snapshot TEXT, model_code_snapshot TEXT, location_name_snapshot TEXT,
 created_at TEXT NOT NULL, confirmed_at TEXT, cancelled_at TEXT);
CREATE TABLE logistics_documents(
 id INTEGER PRIMARY KEY, document_no TEXT UNIQUE, type TEXT NOT NULL CHECK(type IN ('Transfer','Outbound')),
 source_location_id INTEGER NOT NULL REFERENCES locations(id), destination_location_id INTEGER NOT NULL REFERENCES locations(id),
 business_at TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'Draft' CHECK(status IN ('Draft','Confirmed','Cancelled')),
 note TEXT NOT NULL DEFAULT '', source_name_snapshot TEXT, destination_name_snapshot TEXT,
 created_at TEXT NOT NULL, confirmed_at TEXT, cancelled_at TEXT, CHECK(source_location_id <> destination_location_id));
CREATE TABLE logistics_document_lines(
 id INTEGER PRIMARY KEY, document_id INTEGER NOT NULL REFERENCES logistics_documents(id) ON DELETE CASCADE,
 batch_id INTEGER NOT NULL REFERENCES inbound_batches(id), usage_type_id INTEGER NOT NULL REFERENCES usage_types(id),
 quantity INTEGER NOT NULL CHECK(quantity > 0),
 batch_no_snapshot TEXT, material_name_snapshot TEXT,
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
