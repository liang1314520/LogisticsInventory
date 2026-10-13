#include "batch_tracking_page.h"

#include "services/inventory_query_service.h"

#include <QComboBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QTableWidget *makeTable(const QString &name,const QStringList &headers,QWidget *parent)
{
    auto *table=new QTableWidget(0,headers.size(),parent); table->setObjectName(name); table->setHorizontalHeaderLabels(headers); table->setEditTriggers(QAbstractItemView::NoEditTriggers); return table;
}
void setCells(QTableWidget *table,int row,const QStringList &values){for(int column=0;column<values.size();++column)table->setItem(row,column,new QTableWidgetItem(values.at(column)));}
}
BatchTrackingPage::BatchTrackingPage(InventoryQueryService &service,QWidget *parent):QWidget(parent),service_(service)
{
    setObjectName(QStringLiteral("batchTrackingPage")); auto *layout=new QVBoxLayout(this);
    selector_=new QComboBox(this); selector_->setObjectName(QStringLiteral("batchSelector")); layout->addWidget(selector_);
    summary_=new QLabel(this); summary_->setObjectName(QStringLiteral("batchSummary")); layout->addWidget(summary_);
    distribution_=makeTable(QStringLiteral("distribution"),{QStringLiteral("库位"),QStringLiteral("数量")},this); layout->addWidget(distribution_);
    timeline_=makeTable(QStringLiteral("timeline"),{QStringLiteral("业务时间"),QStringLiteral("单号"),QStringLiteral("方向"),QStringLiteral("数量"),QStringLiteral("库位")},this); layout->addWidget(timeline_);
    error_=new QLabel(this); error_->setObjectName(QStringLiteral("errorLabel")); error_->setStyleSheet(QStringLiteral("color:#b00020")); layout->addWidget(error_);
    connect(selector_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int){loadSelected();}); refresh();
}
Result<bool> BatchTrackingPage::refresh()
{
    const EntityId selected=selector_->currentData().toLongLong(); const auto result=service_.historicalBatches(); if(!result.ok()){showError(result.error());return Result<bool>::failure(result.error());}
    selector_->blockSignals(true); selector_->clear(); for(const auto &batch:result.value())selector_->addItem(batch.batchNo,batch.batchId); selector_->blockSignals(false);
    if(selected){const auto selection=selectBatch(selected);if(!selection.ok())return selection;} else if(selector_->count())return loadSelected(); else{summary_->clear();distribution_->setRowCount(0);timeline_->setRowCount(0);error_->clear();} return Result<bool>::success(true);
}
Result<bool> BatchTrackingPage::selectBatch(EntityId batchId)
{
    const int index=selector_->findData(batchId); if(index<0){ServiceError error{QStringLiteral("not_found"),QStringLiteral("批次不存在"),QStringLiteral("batchId")};showError(error);return Result<bool>::failure(error);} const QSignalBlocker blocker(selector_); selector_->setCurrentIndex(index); return loadSelected();
}
Result<bool> BatchTrackingPage::loadSelected()
{
    const EntityId id=selector_->currentData().toLongLong(); if(!id)return Result<bool>::success(true);
    const auto distribution=service_.batchLocationBalances(id); if(!distribution.ok()){showError(distribution.error());return Result<bool>::failure(distribution.error());}
    const auto timeline=service_.batchTimeline(id); if(!timeline.ok()){showError(timeline.error());return Result<bool>::failure(timeline.error());}
    distribution_->setRowCount(distribution.value().size());for(int row=0;row<distribution.value().size();++row){const auto &v=distribution.value().at(row);setCells(distribution_,row,{v.locationName,QString::number(v.quantity)});}
    timeline_->setRowCount(timeline.value().size());for(int row=0;row<timeline.value().size();++row){const auto &v=timeline.value().at(row);setCells(timeline_,row,{v.businessAt.toString(QStringLiteral("yyyy-MM-dd HH:mm")),v.sourceDocument,v.direction,QString::number(v.quantityDelta),v.locationName});}
    if(!timeline.value().isEmpty()){const auto &v=timeline.value().front();const auto remaining=service_.batchRemaining(id);if(!remaining.ok()){showError(remaining.error());return Result<bool>::failure(remaining.error());}summary_->setText(QStringLiteral("%1 / %2 / %3 / %4").arg(v.batchNo).arg(v.categoryName).arg(v.typeCode).arg(remaining.value()));} error_->clear();return Result<bool>::success(true);
}
void BatchTrackingPage::showError(const ServiceError &error){error_->setText(error.message);}
