#include "inventory_page.h"

#include "services/inventory_query_service.h"

#include <QDate>
#include <QDateEdit>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QTableWidget *table(const QString &name,const QStringList &headers,QWidget *parent)
{
    auto *value=new QTableWidget(0,headers.size(),parent); value->setObjectName(name);
    value->setHorizontalHeaderLabels(headers); value->setEditTriggers(QAbstractItemView::NoEditTriggers); return value;
}
void cells(QTableWidget *table,int row,const QStringList &values)
{
    for(int column=0;column<values.size();++column) table->setItem(row,column,new QTableWidgetItem(values.at(column)));
}
}

InventoryPage::InventoryPage(InventoryQueryService &service,QWidget *parent):QWidget(parent),service_(service)
{
    setObjectName(QStringLiteral("inventoryPage")); auto *layout=new QVBoxLayout(this);
    auto *tabs=new QTabWidget(this); tabs->setObjectName(QStringLiteral("inventoryTabs"));
    batches_=table(QStringLiteral("batchBalances"),{QStringLiteral("批次号"),QStringLiteral("分类"),QStringLiteral("类型编码"),QStringLiteral("数量")},tabs);
    locations_=table(QStringLiteral("locationBalances"),{QStringLiteral("库位"),QStringLiteral("数量")},tabs);
    auto *shipmentPage=new QWidget(tabs); auto *shipmentLayout=new QVBoxLayout(shipmentPage); auto *filters=new QHBoxLayout;
    from_=new QDateEdit(QDate::currentDate().addMonths(-1),shipmentPage); from_->setObjectName(QStringLiteral("fromDate")); from_->setCalendarPopup(true);
    to_=new QDateEdit(QDate::currentDate(),shipmentPage); to_->setObjectName(QStringLiteral("toDate")); to_->setCalendarPopup(true);
    auto *button=new QPushButton(QStringLiteral("刷新"),shipmentPage); filters->addWidget(from_); filters->addWidget(to_); filters->addWidget(button); filters->addStretch(); shipmentLayout->addLayout(filters);
    shipments_=table(QStringLiteral("productTypeShipmentSummary"),{QStringLiteral("分类"),QStringLiteral("类型编码"),QStringLiteral("出库数量")},shipmentPage); shipmentLayout->addWidget(shipments_);
    tabs->addTab(batches_,QStringLiteral("批次库存")); tabs->addTab(locations_,QStringLiteral("库位库存")); tabs->addTab(shipmentPage,QStringLiteral("产品类型出库")); layout->addWidget(tabs);
    error_=new QLabel(this); error_->setObjectName(QStringLiteral("errorLabel")); error_->setStyleSheet(QStringLiteral("color:#b00020")); layout->addWidget(error_);
    connect(button,&QPushButton::clicked,this,[this]{refreshUsage();}); refresh();
}

Result<bool> InventoryPage::refresh()
{
    const auto batches=service_.batchBalances(); if(!batches.ok()){showError(batches.error());return Result<bool>::failure(batches.error());}
    const auto locations=service_.locationBalances(); if(!locations.ok()){showError(locations.error());return Result<bool>::failure(locations.error());}
    batches_->setRowCount(batches.value().size()); for(int row=0;row<batches.value().size();++row){const auto &v=batches.value().at(row);cells(batches_,row,{v.batchNo,v.categoryName,v.typeCode,QString::number(v.quantity)});}
    locations_->setRowCount(locations.value().size()); for(int row=0;row<locations.value().size();++row){const auto &v=locations.value().at(row);cells(locations_,row,{v.locationName,QString::number(v.quantity)});}
    return refreshUsage();
}

Result<bool> InventoryPage::refreshUsage()
{
    const auto result=service_.productTypeShipmentSummary({from_->date(),to_->date()}); if(!result.ok()){showError(result.error());return Result<bool>::failure(result.error());}
    shipments_->setRowCount(result.value().size()); for(int row=0;row<result.value().size();++row){const auto &v=result.value().at(row);cells(shipments_,row,{v.categoryName,v.typeCode,QString::number(v.quantity)});} error_->clear(); return Result<bool>::success(true);
}
void InventoryPage::showError(const ServiceError &error){error_->setText(error.message);}
