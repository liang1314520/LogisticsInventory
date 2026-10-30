ALTER TABLE product_models ADD COLUMN units_per_box INTEGER NOT NULL DEFAULT 0
 CHECK(units_per_box >= 0);
ALTER TABLE product_models ADD COLUMN boxes_per_carton INTEGER NOT NULL DEFAULT 0
 CHECK(boxes_per_carton >= 0);

ALTER TABLE logistics_document_lines ADD COLUMN carton_count INTEGER NOT NULL DEFAULT 0
 CHECK(carton_count >= 0);
ALTER TABLE logistics_document_lines ADD COLUMN box_count INTEGER NOT NULL DEFAULT 0
 CHECK(box_count >= 0);
ALTER TABLE logistics_document_lines ADD COLUMN loose_quantity INTEGER NOT NULL DEFAULT 0
 CHECK(loose_quantity >= 0);
ALTER TABLE logistics_document_lines ADD COLUMN units_per_box_snapshot INTEGER NOT NULL DEFAULT 0
 CHECK(units_per_box_snapshot >= 0);
ALTER TABLE logistics_document_lines ADD COLUMN boxes_per_carton_snapshot INTEGER NOT NULL DEFAULT 0
 CHECK(boxes_per_carton_snapshot >= 0);

UPDATE logistics_document_lines SET loose_quantity=quantity;
