#include "reports_page.h"

#include "services/inventory_query_service.h"
#include "services/report_service.h"

#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QVBoxLayout>

ReportsPage::ReportsPage(InventoryQueryService &queries,ReportService &reports,QWidget *parent):QWidget(parent),queries_(queries),reports_(reports)
{
    setObjectName(QStringLiteral("reportsPage")); auto *layout=new QVBoxLayout(this); auto *controls=new QHBoxLayout;
    report_=new QComboBox(this); report_->setObjectName(QStringLiteral("reportSelector")); report_->addItem(QStringLiteral("批次库存"),0); report_->addItem(QStringLiteral("库位库存"),1); report_->addItem(QStringLiteral("产品类型出库"),2);
    from_=new QDateEdit(QDate::currentDate().addMonths(-1),this); from_->setObjectName(QStringLiteral("fromDate")); from_->setCalendarPopup(true);
    to_=new QDateEdit(QDate::currentDate(),this); to_->setObjectName(QStringLiteral("toDate")); to_->setCalendarPopup(true);
    auto *button=new QPushButton(QStringLiteral("导出 CSV"),this); button->setObjectName(QStringLiteral("exportButton")); controls->addWidget(report_); controls->addWidget(from_); controls->addWidget(to_); controls->addWidget(button); controls->addStretch(); layout->addLayout(controls);
    error_=new QLabel(this); error_->setObjectName(QStringLiteral("errorLabel")); error_->setStyleSheet(QStringLiteral("color:#b00020")); layout->addWidget(error_); layout->addStretch();
    savePath_=[](QWidget *parent){return QFileDialog::getSaveFileName(parent,QStringLiteral("导出报表"),{},QStringLiteral("CSV 文件 (*.csv)"));}; connect(button,&QPushButton::clicked,this,[this]{exportCurrent();});
}
Result<bool> ReportsPage::refresh(){error_->clear();return Result<bool>::success(true);}
void ReportsPage::setSavePathCallback(SavePathCallback callback){savePath_=std::move(callback);}
Result<CsvReport> ReportsPage::buildReport() const
{
    CsvReport report; const int type=report_->currentData().toInt();
    if(type==0){const auto result=queries_.batchBalances();if(!result.ok())return Result<CsvReport>::failure(result.error());report.headers={QStringLiteral("批次号"),QStringLiteral("分类"),QStringLiteral("类型编码"),QStringLiteral("数量")};for(const auto &v:result.value())report.rows.append({v.batchNo,v.categoryName,v.typeCode,v.quantity});}
    else if(type==1){const auto result=queries_.locationBalances();if(!result.ok())return Result<CsvReport>::failure(result.error());report.headers={QStringLiteral("库位"),QStringLiteral("数量")};for(const auto &v:result.value())report.rows.append({v.locationName,v.quantity});}
    else {const auto result=queries_.productTypeShipmentSummary({from_->date(),to_->date()});if(!result.ok())return Result<CsvReport>::failure(result.error());report.headers={QStringLiteral("分类"),QStringLiteral("类型编码"),QStringLiteral("出库数量")};for(const auto &v:result.value())report.rows.append({v.categoryName,v.typeCode,v.quantity});}
    return Result<CsvReport>::success(report);
}
bool ReportsPage::exportCurrent()
{
    QString path=savePath_?savePath_(this):QString(); if(path.isEmpty())return false; if(!path.endsWith(QStringLiteral(".csv"),Qt::CaseInsensitive))path+=QStringLiteral(".csv");
    const auto report=buildReport();if(!report.ok()){showError(report.error());return false;} QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)){showError({QStringLiteral("file_open_failed"),file.errorString(),{}});return false;}
    const auto result=reports_.exportCsv(report.value(),&file);if(!result.ok()){file.cancelWriting();showError(result.error());return false;}if(!file.commit()){showError({QStringLiteral("file_commit_failed"),file.errorString(),{}});return false;}error_->clear();return true;
}
void ReportsPage::showError(const ServiceError &error){error_->setText(error.message);}
