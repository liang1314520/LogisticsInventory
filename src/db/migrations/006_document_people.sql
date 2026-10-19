CREATE TABLE personnel(
 id INTEGER PRIMARY KEY,
 name TEXT NOT NULL UNIQUE,
 enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0,1))
);

ALTER TABLE inbound_batches ADD COLUMN received_by_id INTEGER REFERENCES personnel(id);
ALTER TABLE inbound_batches ADD COLUMN received_by_name_snapshot TEXT NOT NULL DEFAULT '';
ALTER TABLE logistics_documents ADD COLUMN outbound_confirmed_by_id INTEGER REFERENCES personnel(id);
ALTER TABLE logistics_documents ADD COLUMN outbound_confirmed_by_name_snapshot TEXT NOT NULL DEFAULT '';
