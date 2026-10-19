CREATE TABLE document_number_sequences (
    prefix TEXT NOT NULL,
    business_date TEXT NOT NULL,
    last_value INTEGER NOT NULL CHECK(last_value >= 0),
    PRIMARY KEY(prefix, business_date)
);
