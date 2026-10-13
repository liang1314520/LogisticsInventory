#pragma once

#include "services/report_service.h"

#include <QMainWindow>

class DashboardPage;
class InboundPage;
class InventoryQueryService;
class InboundService;
class LogisticsService;
class MasterDataService;
class LogisticsPage;
class InventoryPage;
class BatchTrackingPage;
class ReportsPage;
class MasterDataPage;
class QListWidget;
class QStackedWidget;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(MasterDataService &masterData, InventoryQueryService &queries,
               InboundService &inbound, LogisticsService &logistics,
               QWidget *parent = nullptr);
    void setPage(int index, QWidget *page);

private slots:
    void refreshAfterMutation();
    void refreshCurrentPage();

private:
    QListWidget *navigation_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    ReportService reportsService_;
    DashboardPage *dashboard_ = nullptr;
    InboundPage *inbound_ = nullptr;
    LogisticsPage *transfer_ = nullptr;
    LogisticsPage *outbound_ = nullptr;
    InventoryPage *inventory_ = nullptr;
    BatchTrackingPage *batchTracking_ = nullptr;
    ReportsPage *reports_ = nullptr;
    MasterDataPage *masterData_ = nullptr;
};
