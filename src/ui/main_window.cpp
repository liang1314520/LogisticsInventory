#include "main_window.h"

#include "services/inventory_query_service.h"

#include "ui/pages/dashboard_page.h"
#include "ui/pages/inbound_page.h"
#include "ui/pages/logistics_page.h"
#include "ui/pages/batch_tracking_page.h"
#include "ui/pages/inventory_page.h"
#include "ui/pages/master_data_page.h"
#include "ui/pages/reports_page.h"
#include "ui/widgets/workflow_bar.h"
#include "ui/widgets/layout_utils.h"

#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QFontInfo>
#include <QFontMetrics>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>

namespace {
void resizeTableRows(QTableWidget *table)
{
    QStyleOptionComboBox option;
    option.initFrom(table);
    const int textHeight = table->fontMetrics().height();
    const int controlHeight = table->style()->sizeFromContents(
        QStyle::CT_ComboBox, &option, QSize(0, textHeight), table).height();
    const int height = std::max(textHeight + 8, controlHeight + 4);
    table->verticalHeader()->setMinimumSectionSize(height);
    table->verticalHeader()->setDefaultSectionSize(height);
}
}

MainWindow::MainWindow(MasterDataService &masterData, InventoryQueryService &queries,
                       InboundService &inbound, LogisticsService &logistics,
                       BatchWorkflowService &workflow, QWidget *parent)
    : QMainWindow(parent), defaultFont_(QApplication::font()), workflowService_(workflow),
      queries_(queries)
{
    setObjectName(QStringLiteral("mainWindow"));
    QSettings settings;
    bool validFontSize = false;
    const int savedFontSize = settings.value(QStringLiteral("ui/fontSize")).toInt(&validFontSize);
    if (validFontSize && savedFontSize >= 8 && savedFontSize <= 24) {
        QFont font = defaultFont_;
        font.setPointSize(savedFontSize);
        QApplication::setFont(font);
    }

    auto *toolbar = addToolBar(QStringLiteral("显示设置"));
    toolbar->setObjectName(QStringLiteral("displayToolbar"));
    toolbar->setMovable(false);
    toolbar->addWidget(new QLabel(QStringLiteral("字号"), toolbar));
    fontSize_ = new QSpinBox(toolbar);
    fontSize_->setObjectName(QStringLiteral("fontSize"));
    fontSize_->setRange(8, 24);
    fontSize_->setSuffix(QStringLiteral(" pt"));
    fontSize_->setValue(QFontInfo(QApplication::font()).pointSize());
    toolbar->addWidget(fontSize_);
    auto *resetFont = new QPushButton(QStringLiteral("恢复默认"), toolbar);
    resetFont->setObjectName(QStringLiteral("resetFontButton"));
    toolbar->addWidget(resetFont);

    splitter_ = new QSplitter(Qt::Vertical, this);
    splitter_->setObjectName(QStringLiteral("workflowSplitter"));
    splitter_->setChildrenCollapsible(false);
    workflowBar_ = new WorkflowBar(workflowService_, splitter_);
    workflowBar_->setObjectName(QStringLiteral("workflowBar"));
    auto *workspace = new QWidget(splitter_);
    workspace->setMinimumHeight(180);
    workspace->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    auto *layout = new QHBoxLayout(workspace);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    navigation_ = new QListWidget(workspace); navigation_->setObjectName(QStringLiteral("navigation"));
    const QList<QPair<QString, int>> navigationItems{
        {QStringLiteral("工作台"), 0}, {QStringLiteral("01 基础资料"), 7},
        {QStringLiteral("02 入库管理"), 1}, {QStringLiteral("03 调拨管理"), 2},
        {QStringLiteral("04 出库管理"), 3}, {QStringLiteral("05 退回管理"), 8},
        {QStringLiteral("06 批次追踪"), 5}, {QStringLiteral("07 库存查询"), 4},
        {QStringLiteral("08 统计报表"), 6}};
    for (const auto &entry : navigationItems) {
        auto *item = new QListWidgetItem(entry.first, navigation_);
        item->setData(Qt::UserRole, entry.second);
    }
    pages_ = new QStackedWidget(workspace); pages_->setObjectName(QStringLiteral("pages"));
    dashboard_ = new DashboardPage(queries); inbound_ = new InboundPage(masterData, inbound);
    transfer_ = new LogisticsPage(masterData, queries, logistics, DocumentType::Transfer, pages_,
                                  &workflowService_);
    outbound_ = new LogisticsPage(masterData, queries, logistics, DocumentType::Outbound, pages_,
                                  &workflowService_);
    inventory_ = new InventoryPage(queries, pages_);
    batchTracking_ = new BatchTrackingPage(queries, pages_);
    reports_ = new ReportsPage(queries, reportsService_, pages_);
    masterData_ = new MasterDataPage(masterData, pages_);
    pages_->addWidget(dashboard_); pages_->addWidget(inbound_); pages_->addWidget(transfer_); pages_->addWidget(outbound_);
    pages_->addWidget(inventory_); pages_->addWidget(batchTracking_); pages_->addWidget(reports_); pages_->addWidget(masterData_);
    returns_ = new LogisticsPage(masterData, queries, logistics, DocumentType::Return, pages_,
                                &workflowService_);
    pages_->addWidget(returns_);
    layout->addWidget(navigation_); layout->addWidget(pages_, 1);
    splitter_->addWidget(workflowBar_);
    splitter_->addWidget(workspace);
    LayoutUtils::configureVerticalSplitter(splitter_,
        QStringLiteral("上下拖动，调整步骤区高度"));
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 1);
    setCentralWidget(splitter_);
    connect(navigation_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0)
            return;
        pages_->setCurrentIndex(navigation_->item(row)->data(Qt::UserRole).toInt());
        refreshCurrentPage();
    });
    connect(workflowBar_, &WorkflowBar::batchChanged, this,
            [this](EntityId id) { setCurrentBatch(id); });
    connect(inbound_, &InboundPage::batchSelected, this,
            [this](EntityId id) { setCurrentBatch(id); });
    connect(transfer_, &LogisticsPage::batchSelected, this,
            [this](EntityId id) { setCurrentBatch(id); });
    connect(outbound_, &LogisticsPage::batchSelected, this,
            [this](EntityId id) { setCurrentBatch(id); });
    connect(returns_, &LogisticsPage::batchSelected, this,
            [this](EntityId id) { setCurrentBatch(id); });
    connect(workflowBar_, &WorkflowBar::nextRequested, this,
            [this](int page, EntityId id) {
                setCurrentBatch(id);
                selectPage(page);
            });
    connect(workflowBar_, &WorkflowBar::trackingRequested, this,
            [this](EntityId id) {
                setCurrentBatch(id);
                selectPage(5);
            });
    connect(inbound_, &InboundPage::dataChanged, this, &MainWindow::refreshAfterMutation);
    connect(transfer_, &LogisticsPage::dataChanged, this, &MainWindow::refreshAfterMutation);
    connect(outbound_, &LogisticsPage::dataChanged, this, &MainWindow::refreshAfterMutation);
    connect(returns_, &LogisticsPage::dataChanged, this, &MainWindow::refreshAfterMutation);
    connect(masterData_, &MasterDataPage::dataChanged, this, &MainWindow::refreshAfterMutation);
    connect(fontSize_, &QSpinBox::valueChanged, this, &MainWindow::applyFontSize);
    connect(resetFont, &QPushButton::clicked, this, [this] {
        QApplication::setFont(defaultFont_);
        const QSignalBlocker blocker(fontSize_);
        fontSize_->setValue(QFontInfo(defaultFont_).pointSize());
        QSettings().remove(QStringLiteral("ui/fontSize"));
        updateFontMetrics();
    });
    connect(workflowBar_, &WorkflowBar::expandedChanged, this, [this](bool expanded) {
        if (!expanded)
            expandedSplitterState_ = splitter_->saveState();
        updateWorkflowGeometry();
        if (expanded)
            restoreWorkflowLayout(expandedSplitterState_);
        savePreferences();
    });
    connect(splitter_, &QSplitter::splitterMoved, this, [this] { savePreferences(); });
    qApp->installEventFilter(this);
    updateFontMetrics();
    navigation_->setCurrentRow(0);
    if (workflowBar_->refresh().ok())
        refreshWorkflowContext();
    resize(1100, 760);
    const QByteArray savedSplitter = settings.value(QStringLiteral("ui/workflowSplitter")).toByteArray();
    const bool expanded = settings.value(QStringLiteral("ui/workflowExpanded"), true).toBool();
    QTimer::singleShot(0, this, [this, savedSplitter, expanded] {
        restoreWorkflowLayout(savedSplitter);
        expandedSplitterState_ = splitter_->saveState();
        workflowBar_->setExpanded(expanded);
        updateWorkflowGeometry();
    });
}

void MainWindow::selectPage(int pageId)
{
    for (int row = 0; row < navigation_->count(); ++row) {
        if (navigation_->item(row)->data(Qt::UserRole).toInt() != pageId)
            continue;
        navigation_->setCurrentRow(row);
        return;
    }
}

void MainWindow::applyFontSize(int points)
{
    QFont font = defaultFont_;
    font.setPointSize(points);
    QApplication::setFont(font);
    QSettings().setValue(QStringLiteral("ui/fontSize"), points);
    updateFontMetrics();
}

void MainWindow::updateFontMetrics()
{
    for (QWidget *widget : QApplication::allWidgets()) {
        if (auto *table = qobject_cast<QTableWidget *>(widget)) {
            resizeTableRows(table);
            LayoutUtils::fitTableColumns(table);
        }
    }
    const QFontMetrics metrics(navigation_->font());
    int width = 0;
    for (int row = 0; row < navigation_->count(); ++row) {
        auto *item = navigation_->item(row);
        width = std::max(width, metrics.horizontalAdvance(item->text()));
        item->setSizeHint(QSize(0, metrics.height() + 12));
    }
    navigation_->setFixedWidth(width + navigation_->frameWidth() * 2 +
        navigation_->style()->pixelMetric(QStyle::PM_ScrollBarExtent) + 24);
    updateWorkflowGeometry();
}

void MainWindow::updateWorkflowGeometry()
{
    const bool expanded = workflowBar_->isExpanded();
    const int height = expanded ? workflowBar_->minimumSizeHint().height()
        : std::max(workflowBar_->layout()->totalHeightForWidth(workflowBar_->width()),
                   workflowBar_->sizeHint().height());
    const int maximumHeight = expanded ? QWIDGETSIZE_MAX : height;
    if (workflowBar_->maximumHeight() != maximumHeight)
        workflowBar_->setMaximumHeight(maximumHeight);
    if (workflowBar_->minimumHeight() != height)
        workflowBar_->setMinimumHeight(height);
}

void MainWindow::restoreWorkflowLayout(const QByteArray &state)
{
    const QSignalBlocker blocker(splitter_);
    const bool restored = !state.isEmpty() && splitter_->restoreState(state);
    LayoutUtils::configureVerticalSplitter(splitter_,
        QStringLiteral("上下拖动，调整步骤区高度"));
    const auto sizes = splitter_->sizes();
    if (restored && sizes.at(0) > 0 && sizes.at(1) > 0)
        return;
    const int available = std::max(0, splitter_->height() - splitter_->handleWidth());
    const int height = std::min(workflowBar_->expandedHeightForWidth(splitter_->width()),
                               std::max(0, available - splitter_->widget(1)->minimumHeight()));
    splitter_->setSizes({height, available - height});
}

void MainWindow::savePreferences()
{
    QSettings settings;
    if (workflowBar_->isExpanded())
        expandedSplitterState_ = splitter_->saveState();
    settings.setValue(QStringLiteral("ui/workflowSplitter"), expandedSplitterState_);
    settings.setValue(QStringLiteral("ui/workflowExpanded"), workflowBar_->isExpanded());
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Show || event->type() == QEvent::FontChange) {
        if (auto *table = qobject_cast<QTableWidget *>(watched)) {
            resizeTableRows(table);
            LayoutUtils::fitTableColumns(table);
        }
    }
    if (watched == workflowBar_ &&
        (event->type() == QEvent::LayoutRequest || event->type() == QEvent::Resize))
        updateWorkflowGeometry();
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    savePreferences();
    QMainWindow::closeEvent(event);
}

void MainWindow::setCurrentBatch(EntityId batchId)
{
    if (!workflowBar_->setBatch(batchId).ok())
        return;
    currentBatchId_ = batchId;
    inbound_->setCurrentBatch(batchId);
    transfer_->setCurrentBatch(batchId);
    outbound_->setCurrentBatch(batchId);
    returns_->setCurrentBatch(batchId);
    if (pages_->currentIndex() == 5)
        refreshCurrentPage();
    refreshWorkflowContext();
}

void MainWindow::refreshWorkflowContext()
{
    if (currentBatchId_ <= 0) {
        const QString message = QStringLiteral("前置：准备基础资料\n当前操作：新建入库或选择已有批次");
        inbound_->setWorkflowMessage(message);
        returns_->setWorkflowMessage(message);
        for (auto *page : {transfer_, outbound_})
            page->setWorkflowMessage(QStringLiteral("当前范围：全部批次（出库仍按来源条件筛选）\n当前操作：查看单据；新建时可手动选择明细批次"));
        return;
    }
    const auto state = workflowService_.state(currentBatchId_);
    if (!state.ok())
        return;
    inbound_->setWorkflowMessage(QStringLiteral("前置：%1\n当前操作：%2")
                                  .arg(state.value().steps.at(1).prerequisiteMessage.isEmpty()
                                           ? QStringLiteral("基础资料已准备")
                                           : state.value().steps.at(1).prerequisiteMessage,
                                       state.value().nextActionText));
    transfer_->setWorkflowMessage(QStringLiteral("前置：%1\n当前操作：%2")
                                  .arg(state.value().steps.at(2).prerequisiteMessage.isEmpty()
                                           ? QStringLiteral("公司与工厂均计库存，账面余额不足也可补录")
                                           : state.value().steps.at(2).prerequisiteMessage,
                                       state.value().steps.at(2).actionText.isEmpty()
                                           ? state.value().nextActionText
                                           : state.value().steps.at(2).actionText));
    outbound_->setWorkflowMessage(QStringLiteral("前置：%1\n当前操作：%2")
                                  .arg(state.value().steps.at(3).prerequisiteMessage.isEmpty()
                                           ? QStringLiteral("工厂发往不计库存的 Amazon 仓，允许先补录出库")
                                           : state.value().steps.at(3).prerequisiteMessage,
                                       state.value().steps.at(3).actionText.isEmpty()
                                           ? state.value().nextActionText
                                           : state.value().steps.at(3).actionText));
    returns_->setWorkflowMessage(QStringLiteral("前置：%1\n当前操作：%2")
                                 .arg(state.value().steps.at(4).prerequisiteMessage.isEmpty()
                                          ? QStringLiteral("仅实际退回公司时登记，总库存不变，账面余额不足也可补录")
                                          : state.value().steps.at(4).prerequisiteMessage,
                                      state.value().steps.at(4).actionText.isEmpty()
                                          ? state.value().nextActionText
                                          : state.value().steps.at(4).actionText));
}

EntityId MainWindow::currentBatchId() const
{
    return currentBatchId_;
}

void MainWindow::setPage(int index, QWidget *page)
{
    if (!page || index < 4 || index > 7) return;
    QWidget *old = pages_->widget(index);
    if (page == old) return;
    const bool current = pages_->currentIndex() == index;
    pages_->removeWidget(old);
    pages_->insertWidget(index, page);
    old->deleteLater();
    if (current) pages_->setCurrentIndex(index);
}

void MainWindow::refreshAfterMutation()
{
    if (workflowBar_->refresh().ok())
        refreshWorkflowContext();
    dashboard_->refresh();
    refreshCurrentPage();
    const auto negatives = queries_.negativeBalanceCount();
    if (!negatives.ok())
        statusBar()->showMessage(QStringLiteral("记录变更已保存；负库存提示暂不可用：%1")
                                     .arg(negatives.error().message));
    else
        statusBar()->showMessage(negatives.value() > 0
            ? QStringLiteral("记录变更已保存；存在 %1 处负库存，请核对或补录相关记录").arg(negatives.value())
            : QStringLiteral("记录变更已保存"));
}

void MainWindow::refreshCurrentPage()
{
    switch (pages_->currentIndex()) {
    case 0: dashboard_->refresh(); break;
    case 1: inbound_->refresh(); break;
    case 2: transfer_->refresh(); break;
    case 3: outbound_->refresh(); break;
    case 4: if(auto *page=qobject_cast<InventoryPage *>(pages_->currentWidget()))page->refresh(); break;
    case 5:
        if (auto *page = qobject_cast<BatchTrackingPage *>(pages_->currentWidget())) {
            if (page->refresh().ok() && currentBatchId_ > 0)
                page->selectBatch(currentBatchId_);
        }
        break;
    case 6: if(auto *page=qobject_cast<ReportsPage *>(pages_->currentWidget()))page->refresh(); break;
    case 7: if(auto *page=qobject_cast<MasterDataPage *>(pages_->currentWidget()))page->refresh(); break;
    case 8: returns_->refresh(); break;
    default: break;
    }
}
