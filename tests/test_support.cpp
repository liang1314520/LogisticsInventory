#include "test_support.h"

#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>

TestDatabase::TestDatabase()
    : database_(QStringLiteral("test-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

bool TestDatabase::open()
{
    QString error;
    return directory_.isValid()
        && database_.open(directory_.filePath(QStringLiteral("test.sqlite")), &error);
}

bool TestDatabase::migrate()
{
    QString error;
    return database_.migrate(&error);
}

bool TestDatabase::openAndMigrate()
{
    return open() && migrate();
}

bool TestDatabase::executeResource(const QString &resourcePath)
{
    QFile resource(resourcePath);
    if (!resource.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QStringList statements = QString::fromUtf8(resource.readAll()).split(QLatin1Char(';'));
    for (const QString &statement : statements) {
        const QString sql = statement.trimmed();
        if (!sql.isEmpty()) {
            QSqlQuery query(database_.connection());
            if (!query.exec(sql))
                return false;
        }
    }
    return true;
}

Database &TestDatabase::database()
{
    return database_;
}

QVariant TestDatabase::scalar(const QString &sql) const
{
    QSqlQuery query(database_.connection());
    if (!query.exec(sql) || !query.next())
        return {};
    return query.value(0);
}

bool TestDatabase::tableExists(const QString &tableName) const
{
    QSqlQuery query(database_.connection());
    query.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?"));
    query.addBindValue(tableName);
    return query.exec() && query.next();
}

bool TestDatabase::execute(const QString &sql) const
{
    QSqlQuery query(database_.connection());
    return query.exec(sql);
}
