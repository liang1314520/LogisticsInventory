#include "database.h"

#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>
#include <utility>

namespace {
void setError(QString *target, const QString &message)
{
    if (target)
        *target = message;
}

QString beginStatement(Database::TransactionMode mode)
{
    switch (mode) {
    case Database::TransactionMode::Deferred:
        return QStringLiteral("BEGIN DEFERRED");
    case Database::TransactionMode::Immediate:
        return QStringLiteral("BEGIN IMMEDIATE");
    case Database::TransactionMode::Exclusive:
        return QStringLiteral("BEGIN EXCLUSIVE");
    }
    return QStringLiteral("BEGIN DEFERRED");
}
}

Database::Database(QString connectionName)
    : connectionName_(std::move(connectionName))
{
}

Database::~Database()
{
    {
        QSqlDatabase db = QSqlDatabase::database(connectionName_, false);
        if (db.isValid())
            db.close();
    }
    QSqlDatabase::removeDatabase(connectionName_);
}

bool Database::open(const QString &path, QString *error)
{
    Q_INIT_RESOURCE(migrations);

    if (QSqlDatabase::contains(connectionName_)) {
        setError(error, QStringLiteral("Database connection already exists: %1").arg(connectionName_));
        return false;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    db.setDatabaseName(path);
    if (!db.open()) {
        setError(error, db.lastError().text());
        return false;
    }
    return execute(QStringLiteral("PRAGMA foreign_keys = ON"), error);
}

QSqlDatabase Database::connection() const
{
    return QSqlDatabase::database(connectionName_, false);
}

bool Database::execute(const QString &sql, QString *error)
{
    QSqlQuery query(connection());
    if (query.exec(sql))
        return true;
    setError(error, query.lastError().text());
    return false;
}

bool Database::beginImmediate(QString *error)
{
    return execute(beginStatement(TransactionMode::Immediate), error);
}

bool Database::transaction(TransactionMode mode, const TransactionCallback &callback, QString *error)
{
    if (!connection().isOpen()) {
        setError(error, QStringLiteral("Database is not open"));
        return false;
    }
    if (!callback) {
        setError(error, QStringLiteral("Transaction callback is empty"));
        return false;
    }
    if (!execute(beginStatement(mode), error))
        return false;

    bool succeeded = false;
    try {
        succeeded = callback();
    } catch (...) {
        rollback();
        throw;
    }
    if (!succeeded) {
        rollback();
        if (error && error->isEmpty())
            *error = QStringLiteral("Transaction callback failed");
        return false;
    }
    if (commit(error))
        return true;

    rollback();
    return false;
}

bool Database::commit(QString *error)
{
    return execute(QStringLiteral("COMMIT"), error);
}

void Database::rollback()
{
    QSqlQuery query(connection());
    query.exec(QStringLiteral("ROLLBACK"));
}

bool Database::executeMigration(const QString &resourcePath, QString *error)
{
    QFile migration(resourcePath);
    if (!migration.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(error, migration.errorString());
        return false;
    }

    const QStringList statements = QString::fromUtf8(migration.readAll()).split(QLatin1Char(';'));
    for (const QString &statement : statements) {
        const QString sql = statement.trimmed();
        if (!sql.isEmpty() && !execute(sql, error))
            return false;
    }
    return true;
}

bool Database::foreignKeyCheck(QString *error)
{
    QSqlQuery query(connection());
    if (!query.exec(QStringLiteral("PRAGMA foreign_key_check"))) {
        setError(error, query.lastError().text());
        return false;
    }
    if (query.next()) {
        setError(error, QStringLiteral("Foreign key check failed for table %1 row %2")
                            .arg(query.value(0).toString(), query.value(1).toString()));
        return false;
    }
    return true;
}

bool Database::migrate(QString *error)
{
    return transaction(TransactionMode::Immediate, [this, error]() {
        if (!execute(QStringLiteral("CREATE TABLE IF NOT EXISTS schema_migrations("
                                    "version INTEGER PRIMARY KEY)"), error)) {
            return false;
        }

        QSqlQuery versionQuery(connection());
        if (!versionQuery.exec(QStringLiteral("SELECT COALESCE(MAX(version), 0) FROM schema_migrations"))
            || !versionQuery.next()) {
            setError(error, versionQuery.lastError().text());
            return false;
        }
        const int current = versionQuery.value(0).toInt();
        versionQuery.finish();
        const QStringList resources{
            QStringLiteral(":/migrations/001_initial.sql"),
            QStringLiteral(":/migrations/002_product_types.sql"),
            QStringLiteral(":/migrations/003_product_models.sql"),
            QStringLiteral(":/migrations/004_document_number_sequences.sql"),
            QStringLiteral(":/migrations/005_inbound_material_model.sql"),
            QStringLiteral(":/migrations/006_document_people.sql")
        };
        if (current > resources.size()) {
            setError(error, QStringLiteral("Database schema version %1 is newer than supported version %2")
                                .arg(current).arg(resources.size()));
            return false;
        }
        for (int version = current + 1; version <= resources.size(); ++version) {
            if (!executeMigration(resources.at(version - 1), error))
                return false;
            if ((version == 2 || version == 3) && !foreignKeyCheck(error))
                return false;
            if (!execute(QStringLiteral("INSERT INTO schema_migrations(version) VALUES(%1)").arg(version), error))
                return false;
        }
        return true;
    }, error);
}
