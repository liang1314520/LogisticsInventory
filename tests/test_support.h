#pragma once

#include "db/database.h"

#include <QTemporaryDir>
#include <QVariant>

class TestDatabase {
public:
    TestDatabase();

    bool open();
    bool migrate();
    bool openAndMigrate();
    bool executeResource(const QString &resourcePath);
    Database &database();
    QVariant scalar(const QString &sql) const;
    bool tableExists(const QString &tableName) const;
    bool execute(const QString &sql) const;

private:
    QTemporaryDir directory_;
    Database database_;
};
