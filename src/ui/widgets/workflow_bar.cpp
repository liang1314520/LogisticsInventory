#include "workflow_bar.h"
#include "status_presentation.h"
#include "layout_utils.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {
QString statusText(WorkflowStepStatus status)
{
    switch (status) {
    case WorkflowStepStatus::NotStarted:
        return QStringLiteral("未开始");
    case WorkflowStepStatus::InProgress:
        return QStringLiteral("进行中");
    case WorkflowStepStatus::Completed:
        return QStringLiteral("已完成");
    case WorkflowStepStatus::NotApplicable:
        return QStringLiteral("不适用");
    case WorkflowStepStatus::NeedsAction:
        return QStringLiteral("需处理");
    }
    return QStringLiteral("未开始");
}
}

WorkflowBar::WorkflowBar(BatchWorkflowService &service, QWidget *parent)
    : QWidget(parent), service_(service)
{
    setObjectName(QStringLiteral("workflowBar"));
    auto *layout = new QVBoxLayout(this);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    auto *header = new QHBoxLayout;
    selector_ = new QComboBox(this);
    selector_->setObjectName(QStringLiteral("workflowBatchSelector"));
    selector_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    selector_->setMinimumContentsLength(1);
    selector_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    selector_->view()->setTextElideMode(Qt::ElideNone);
    selector_->view()->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("workflowStatusLabel"));
    header->addWidget(new QLabel(QStringLiteral("当前批次"), this));
    header->addWidget(selector_, 1);
    header->addWidget(status_);
    expand_ = new QPushButton(QStringLiteral("收起步骤"), this);
    expand_->setObjectName(QStringLiteral("workflowExpandButton"));
    expand_->setCheckable(true);
    expand_->setChecked(true);
    header->addWidget(expand_);
    layout->addLayout(header);

    steps_ = new QTableWidget(0, 3, this);
    steps_->setObjectName(QStringLiteral("workflowSteps"));
    steps_->setHorizontalHeaderLabels({QStringLiteral("步骤"), QStringLiteral("状态"),
                                       QStringLiteral("说明")});
    steps_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    steps_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    LayoutUtils::fitTableColumns(steps_);
    layout->addWidget(steps_, 1);

    message_ = new QLabel(this);
    message_->setObjectName(QStringLiteral("workflowMessageLabel"));
    message_->setWordWrap(true);
    layout->addWidget(message_);

    auto *actions = new QHBoxLayout;
    next_ = new QPushButton(QStringLiteral("去下一步"), this);
    next_->setObjectName(QStringLiteral("workflowNextButton"));
    tracking_ = new QPushButton(QStringLiteral("查看批次追踪"), this);
    tracking_->setObjectName(QStringLiteral("workflowTrackingButton"));
    actions->addWidget(next_);
    actions->addWidget(tracking_);
    actions->addStretch();
    layout->addLayout(actions);

    connect(selector_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        const EntityId id = selector_->currentData().toLongLong();
        if (id > 0 && id != batchId_) {
            batchId_ = id;
            if (loadCurrent().ok())
                emit batchChanged(batchId_);
        }
    });
    connect(expand_, &QPushButton::toggled, this, [this](bool expanded) {
        steps_->setVisible(expanded);
        expand_->setText(expanded ? QStringLiteral("收起步骤") : QStringLiteral("展开步骤"));
        this->layout()->invalidate();
        updateGeometry();
        emit expandedChanged(expanded);
    });
    connect(next_, &QPushButton::clicked, this, &WorkflowBar::requestNext);
    connect(tracking_, &QPushButton::clicked, this, &WorkflowBar::requestTracking);
    clearState();
}

Result<bool> WorkflowBar::refresh()
{
    const auto options = service_.batchOptions();
    if (!options.ok()) {
        showError(options.error());
        return Result<bool>::failure(options.error());
    }
    const EntityId selected = batchId_;
    {
        const QSignalBlocker blocker(selector_);
        selector_->clear();
        for (const auto &option : options.value()) {
            selector_->addItem(QStringLiteral("%1 / %2 / 物料型号：%3 / %4")
                                   .arg(option.batchNo, option.productTypeName,
                                        option.materialModel.isEmpty() ? QStringLiteral("未填写")
                                                                       : option.materialModel)
                                   .arg(option.currentQuantity),
                               option.batchId);
            selector_->setItemData(selector_->count() - 1,
                                   selector_->itemText(selector_->count() - 1), Qt::ToolTipRole);
        }
        const int index = selector_->findData(selected);
        selector_->setCurrentIndex(index >= 0 ? index : selector_->count() > 0 ? 0 : -1);
        batchId_ = selector_->currentData().toLongLong();
    }
    const auto loaded = loadCurrent();
    if (loaded.ok() && selected != batchId_)
        emit batchChanged(batchId_);
    return loaded;
}

Result<bool> WorkflowBar::setBatch(EntityId batchId)
{
    if (batchId <= 0) {
        const QSignalBlocker blocker(selector_);
        selector_->setCurrentIndex(-1);
        batchId_ = 0;
        clearState();
        return Result<bool>::success(true);
    }
    const int index = selector_->findData(batchId);
    if (index < 0) {
        const auto refreshed = refresh();
        if (!refreshed.ok())
            return refreshed;
    }
    const int selected = selector_->findData(batchId);
    if (selected < 0) {
        const ServiceError error{QStringLiteral("not_found"), QStringLiteral("批次不存在"),
                                 QStringLiteral("batchId")};
        showError(error);
        return Result<bool>::failure(error);
    }
    {
        const QSignalBlocker blocker(selector_);
        selector_->setCurrentIndex(selected);
    }
    batchId_ = batchId;
    return loadCurrent();
}

EntityId WorkflowBar::currentBatchId() const
{
    return batchId_;
}

void WorkflowBar::setExpanded(bool expanded)
{
    expand_->setChecked(expanded);
}

bool WorkflowBar::isExpanded() const
{
    return expand_->isChecked();
}

int WorkflowBar::expandedHeightForWidth(int width) const
{
    const auto *box = layout();
    const QMargins margins = box->contentsMargins();
    const int contentWidth = std::max(1, width - margins.left() - margins.right());
    int height = margins.top() + margins.bottom();
    for (int index = 0; index < box->count(); ++index) {
        auto *item = box->itemAt(index);
        if (item->widget() == steps_) {
            height += steps_->horizontalHeader()->sizeHint().height() + steps_->frameWidth() * 2 +
                steps_->style()->pixelMetric(QStyle::PM_ScrollBarExtent);
            for (int row = 0; row < 6; ++row)
                height += row < steps_->rowCount() ? steps_->rowHeight(row)
                                                   : steps_->verticalHeader()->defaultSectionSize();
        } else {
            height += item->hasHeightForWidth() ? item->heightForWidth(contentWidth)
                                                : item->sizeHint().height();
        }
    }
    return height + std::max(0, box->count() - 1) * std::max(0, box->spacing());
}

Result<bool> WorkflowBar::loadCurrent()
{
    if (!batchId_) {
        clearState();
        return Result<bool>::success(true);
    }
    const auto result = service_.state(batchId_);
    if (!result.ok()) {
        showError(result.error());
        return Result<bool>::failure(result.error());
    }
    state_ = result.value();
    selector_->setToolTip(selector_->currentText());
    status_->setText(state_.overallStatus);
    StatusPresentation::apply(status_, state_.steps.at(5).status == WorkflowStepStatus::Completed
        ? StatusPresentation::Tone::Success
        : state_.blockingReasons.isEmpty() ? StatusPresentation::Tone::Active
                                          : StatusPresentation::Tone::Attention);
    message_->setStyleSheet({});
    message_->setText(state_.nextActionText);
    steps_->setRowCount(state_.steps.size());
    for (int row = 0; row < state_.steps.size(); ++row) {
        const auto &step = state_.steps.at(row);
        steps_->setItem(row, 0, new QTableWidgetItem(
            QStringLiteral("%1 %2").arg(step.number, 2, 10, QLatin1Char('0')).arg(step.title)));
        steps_->setItem(row, 1, new QTableWidgetItem(statusText(step.status)));
        StatusPresentation::apply(steps_->item(row, 1), StatusPresentation::tone(step.status));
        steps_->setItem(row, 2, new QTableWidgetItem(step.summary));
    }
    LayoutUtils::fitTableColumns(steps_);
    next_->setEnabled(state_.nextPage >= 0 && !state_.availableActions.isEmpty());
    tracking_->setEnabled(true);
    return Result<bool>::success(true);
}

void WorkflowBar::clearState()
{
    state_ = {};
    selector_->setToolTip(selector_->currentText());
    status_->clear();
    status_->setStyleSheet({});
    message_->clear();
    message_->setStyleSheet({});
    steps_->setRowCount(0);
    next_->setEnabled(false);
    tracking_->setEnabled(false);
}

void WorkflowBar::showError(const ServiceError &error)
{
    message_->setText(error.message);
    StatusPresentation::apply(message_, StatusPresentation::Tone::Error);
    next_->setEnabled(false);
}

void WorkflowBar::requestNext()
{
    if (batchId_ && state_.nextPage >= 0)
        emit nextRequested(state_.nextPage, batchId_);
}

void WorkflowBar::requestTracking()
{
    if (batchId_)
        emit trackingRequested(batchId_);
}
