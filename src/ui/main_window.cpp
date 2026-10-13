#include "main_window.h"

#include "ui/pages/dashboard_page.h"
#include "ui/pages/inbound_page.h"
#include "ui/pages/logistics_page.h"
#include "ui/pages/batch_tracking_page.h"
#include "ui/pages/inventory_page.h"
#include "ui/pages/master_data_page.h"
#include "ui/pages/reports_page.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QStackedWidget>

MainWindow::MainWindow(MasterDataService &masterData, InventoryQueryService &queries,
                       InboundService &inbound, LogisticsService &logistics, QWidget *parent)
    : QMainWindow(parent)
{
    setObjectName(QStringLiteral("mainWindow"));
    auto *central = new QWidget(this); auto *layout = new QHBoxLayout(central);
    navigation_ = new QListWidget(central); navigation_->setObjectName(QStringLiteral("navigation"));
    navigation_->addItems({QStringLiteral("工作台"), QStringLiteral("入库管理"),
        QStringLiteral("调拨管理"), QStringLiteral("出库管理"), QStringLiteral("库存查询"),
        QStringLiteral("批次追踪"), QStringLiteral("统计报表"), QStringLiteral("基础资料")});
    pages_ = new QStackedWidget(central); pages_->setObjectName(QStringLiteral("pages"));
    dashboard_ = new DashboardPage(queries); inbound_ = new InboundPage(masterData, inbound);
    transfer_ = new LogisticsPage(masterData, queries, logistics, DocumentType::Transfer);
    outbound_ = new LogisticsPage(masterData, queries, logistics, DocumentType::Outbound);
    inventory_ = new InventoryPage(queries, pages_);
    batchTracking_ = new BatchTrackingPage(queries, pages_);
    reports_ = new ReportsPage(queries, reportsService_, pages_);
    masterData_ = new MasterDataPage(masterData, pages_);
    pages_->addWidget(dashboard_); pages_->addWidget(inbound_); pages_->addWidget(transfer_); pages_->addWidget(outbound_);
    pages_->addWidget(inventory_); pages_->addWidget(batchTracking_); pages_->addWidget(reports_); pages_->addWidget(masterData_);
    layout->addWidget(navigation_); layout->addWidget(pages_, 1); setCentralWidget(central);
    connect(navigation_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
    connect(navigation_, &QListWidget::currentRowChanged, this, &MainWindow::refreshCurrentPage);
    connect(inbound_, &InboundPage::dataChanged, this, &MainWindow::refreshAfterMutation);
    connect(transfer_, &LogisticsPage::dataChanged, this, &MainWindow::refreshAfterMutation);
    connect(outbound_, &LogisticsPage::dataChanged, this, &MainWindow::refreshAfterMutation);
    navigation_->setCurrentRow(0);
}

void MainWindow::setPage(int index, QWidget *page)
{
    if (!page || index < 4 || index > 7) return;
    QWidget *old = pages_->widget(index);
    if (page == old) return;
    pages_->removeWidget(old);
    pages_->insertWidget(index, page);
    old->deleteLater();
    if (navigation_->currentRow() == index) pages_->setCurrentIndex(index);
}

void MainWindow::refreshAfterMutation()
{
    dashboard_->refresh(); refreshCurrentPage();
}

void MainWindow::refreshCurrentPage()
{
    switch (pages_->currentIndex()) {
    case 0: dashboard_->refresh(); break;
    case 1: inbound_->refresh(); break;
    case 2: transfer_->refresh(); break;
    case 3: outbound_->refresh(); break;
    case 4: if(auto *page=qobject_cast<InventoryPage *>(pages_->currentWidget()))page->refresh(); break;
    case 5: if(auto *page=qobject_cast<BatchTrackingPage *>(pages_->currentWidget()))page->refresh(); break;
    case 6: if(auto *page=qobject_cast<ReportsPage *>(pages_->currentWidget()))page->refresh(); break;
    case 7: if(auto *page=qobject_cast<MasterDataPage *>(pages_->currentWidget()))page->refresh(); break;
    default: break;
    }
}
