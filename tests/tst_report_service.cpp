#include "services/report_service.h"

#include <QBuffer>
#include <QtTest>

class FailingDevice : public QIODevice {
public:
    bool open(OpenMode mode) override { setOpenMode(mode); return true; }
protected:
    qint64 readData(char *,qint64) override { return -1; }
    qint64 writeData(const char *,qint64) override { setErrorString(QStringLiteral("forced")); return -1; }
};

class ReportServiceTest : public QObject {
    Q_OBJECT
private slots:
    void csvHasBomHeadersEscapingAndDate();
    void writeFailureIsReturnedAndDeviceRemainsOpen();
};

void ReportServiceTest::csvHasBomHeadersEscapingAndDate()
{
    CsvReport report;
    report.headers={QStringLiteral("单据号"),QStringLiteral("备注"),QStringLiteral("业务时间")};
    report.rows.append({QStringLiteral("O-1"),QStringLiteral("含\"引号\",逗号\n换行"),QDateTime(QDate(2026,9,10),QTime(8,9,10))});
    QByteArray bytes; QBuffer device(&bytes); QVERIFY(device.open(QIODevice::WriteOnly));
    ReportService service; const auto result=service.exportCsv(report,&device); QVERIFY(result.ok()); QVERIFY(device.isOpen());
    const QByteArray expected=QByteArray::fromHex("efbbbf")+QByteArray("单据号,备注,业务时间\r\nO-1,\"含\"\"引号\"\",逗号\r\n换行\",2026-09-10 08:09:10\r\n");
    QCOMPARE(bytes,expected);
}

void ReportServiceTest::writeFailureIsReturnedAndDeviceRemainsOpen()
{
    FailingDevice device; QVERIFY(device.open(QIODevice::WriteOnly)); ReportService service;
    CsvReport report; report.headers={QStringLiteral("标题")};
    const auto result=service.exportCsv(report,&device); QVERIFY(!result.ok()); QCOMPARE(result.error().code,QStringLiteral("csv_write_failed")); QVERIFY(device.isOpen());
}

QTEST_MAIN(ReportServiceTest)
#include "tst_report_service.moc"
