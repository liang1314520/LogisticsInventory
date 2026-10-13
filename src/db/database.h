#pragma once

#include <QSqlDatabase>
#include <QString>
#include <functional>

class Database {
public:
    enum class TransactionMode {
        Deferred,
        Immediate,
        Exclusive
    };
    using TransactionCallback = std::function<bool()>;

    explicit Database(QString connectionName);
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    bool open(const QString &path, QString *error = nullptr);
    bool migrate(QString *error = nullptr);
    bool transaction(TransactionMode mode, const TransactionCallback &callback,
                     QString *error = nullptr);
    QSqlDatabase connection() const;
    bool beginImmediate(QString *error = nullptr);
    bool commit(QString *error = nullptr);
    void rollback();

private:
    bool execute(const QString &sql, QString *error);
    bool executeMigration(const QString &resourcePath, QString *error);
    bool foreignKeyCheck(QString *error);

    QString connectionName_;
};
