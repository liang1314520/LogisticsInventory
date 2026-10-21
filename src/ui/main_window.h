#pragma once

#include "core/models.h"
#include "services/report_service.h"

#include <QByteArray>
#include <QFont>
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
class BatchWorkflowService;
class WorkflowBar;
class QListWidget;
class QStackedWidget;
class QCloseEvent;
class QSpinBox;
class QSplitter;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(MasterDataService &masterData, InventoryQueryService &queries,
               InboundService &inbound, LogisticsService &logistics,
               BatchWorkflowService &workflow, QWidget *parent = nullptr);
    void setPage(int index, QWidget *page);
    void setCurrentBatch(EntityId batchId);
    EntityId currentBatchId() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void refreshAfterMutation();
    void refreshCurrentPage();

private:
    void refreshWorkflowContext();
    void selectPage(int pageId);
    void applyFontSize(int points);
    void updateFontMetrics();
    void updateWorkflowGeometry();
    void restoreWorkflowLayout(const QByteArray &state);
    void savePreferences();
    QSplitter *splitter_ = nullptr;
    QSpinBox *fontSize_ = nullptr;
    QFont defaultFont_;
    QByteArray expandedSplitterState_;
    QListWidget *navigation_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    ReportService reportsService_;
    DashboardPage *dashboard_ = nullptr;
    InboundPage *inbound_ = nullptr;
    LogisticsPage *transfer_ = nullptr;
    LogisticsPage *outbound_ = nullptr;
    LogisticsPage *returns_ = nullptr;
    InventoryPage *inventory_ = nullptr;
    BatchTrackingPage *batchTracking_ = nullptr;
    ReportsPage *reports_ = nullptr;
    MasterDataPage *masterData_ = nullptr;
    BatchWorkflowService &workflowService_;
    InventoryQueryService &queries_;
    WorkflowBar *workflowBar_ = nullptr;
    EntityId currentBatchId_ = 0;
};
