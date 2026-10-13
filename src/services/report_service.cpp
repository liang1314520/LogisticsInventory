#include "report_service.h"

#include <QFileDevice>

namespace {
QString text(const QVariant &value)
{
    if (value.canConvert<QDateTime>() && value.metaType().id()==QMetaType::QDateTime)
        return value.toDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    return value.toString();
}
QString escape(QString value)
{
    value.replace(QLatin1Char('"'),QStringLiteral("\"\""));
    value.replace(QStringLiteral("\r\n"),QStringLiteral("\n"));
    value.replace(QLatin1Char('\r'),QLatin1Char('\n'));
    value.replace(QStringLiteral("\n"),QStringLiteral("\r\n"));
    if(value.contains(QLatin1Char(','))||value.contains(QLatin1Char('"'))||value.contains(QStringLiteral("\r\n")))
        return QLatin1Char('"')+value+QLatin1Char('"');
    return value;
}
}

Result<bool> ReportService::exportCsv(const CsvReport &report,QIODevice *device) const
{
    if(!device||!device->isOpen()||!device->isWritable())
        return Result<bool>::failure({QStringLiteral("csv_write_failed"),QStringLiteral("CSV 输出设备不可写"),{}});
    QByteArray data="\xEF\xBB\xBF";
    QStringList lines; QStringList headers; for(const auto &h:report.headers) headers.append(escape(h)); lines.append(headers.join(QLatin1Char(',')));
    for(const auto &row:report.rows){ QStringList fields; for(const auto &v:row) fields.append(escape(text(v))); lines.append(fields.join(QLatin1Char(','))); }
    data+=lines.join(QStringLiteral("\r\n")).toUtf8(); data+="\r\n";
    qint64 offset=0; while(offset<data.size()){ const qint64 written=device->write(data.constData()+offset,data.size()-offset); if(written<=0) return Result<bool>::failure({QStringLiteral("csv_write_failed"),device->errorString(),{}}); offset+=written; }
    if(auto *file=qobject_cast<QFileDevice *>(device); file && !file->flush()) return Result<bool>::failure({QStringLiteral("csv_write_failed"),file->errorString(),{}});
    return Result<bool>::success(true);
}
