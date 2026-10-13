#pragma once

#include "core/result.h"

#include <QDateTime>
#include <QIODevice>
#include <QStringList>
#include <QVariant>
#include <QVector>

struct CsvReport {
    QStringList headers;
    QVector<QVector<QVariant>> rows;
};

class ReportService {
public:
    Result<bool> exportCsv(const CsvReport &report, QIODevice *device) const;
};
