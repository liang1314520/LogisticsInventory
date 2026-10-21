#include "batch_tracking_page.h"

#include "services/inventory_query_service.h"
#include "ui/widgets/status_presentation.h"
#include "ui/widgets/layout_utils.h"

#include <QComboBox>
#include <QLabel>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

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
    auto *workflowContext = new QLabel(this);
    workflowContext->setObjectName(QStringLiteral("workflowContext"));
    workflowContext->setWordWrap(true);
    workflowContext->setText(QStringLiteral("步骤 06 / 06：继续流转 / 流程结束\n当前操作：查看公司、工厂库存与出库流水"));
    layout->addWidget(workflowContext);
    selector_ = new QComboBox(this);
    selector_->setObjectName(QStringLiteral("batchSelector"));
    selector_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    selector_->setMinimumContentsLength(1);
    selector_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    selector_->view()->setTextElideMode(Qt::ElideNone);
    selector_->view()->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    layout->addWidget(selector_);
    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("batchSummary"));
    summary_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    summary_->setWordWrap(true);
    layout->addWidget(summary_);
    splitter_ = new QSplitter(Qt::Vertical, this);
    splitter_->setObjectName(QStringLiteral("trackingSplitter"));
    distribution_ = makeTable(QStringLiteral("distribution"),
        {QStringLiteral("库位"), QStringLiteral("数量")}, splitter_);
    timeline_ = makeTable(QStringLiteral("timeline"),
        {QStringLiteral("业务时间"), QStringLiteral("单号"), QStringLiteral("方向"),
         QStringLiteral("数量"), QStringLiteral("库位"), QStringLiteral("调拨/出库型号"),
         QStringLiteral("单据状态"), QStringLiteral("物料型号")}, splitter_);
    splitter_->addWidget(distribution_);
    splitter_->addWidget(timeline_);
    distribution_->setMinimumHeight(80);
    timeline_->setMinimumHeight(80);
    splitter_->setStretchFactor(0, 1);
    splitter_->setStretchFactor(1, 1);
    LayoutUtils::configureVerticalSplitter(splitter_,
        QStringLiteral("上下拖动，调整库位分布与业务流水高度"));
    layout->addWidget(splitter_, 1);
    connect(splitter_, &QSplitter::splitterMoved, this, [this] {
        if (splitterInitialized_)
            QSettings().setValue(QStringLiteral("ui/batchTrackingSplitter"), splitter_->saveState());
    });
    error_=new QLabel(this); error_->setObjectName(QStringLiteral("errorLabel")); error_->setStyleSheet(QStringLiteral("color:#b00020")); layout->addWidget(error_);
    connect(selector_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int){loadSelected();}); refresh();
}
void BatchTrackingPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (splitterInitialized_)
        return;
    splitterInitialized_ = true;
    QTimer::singleShot(0, this, [this] {
        const QSignalBlocker blocker(splitter_);
        const auto state = QSettings().value(QStringLiteral("ui/batchTrackingSplitter")).toByteArray();
        const bool restored = !state.isEmpty() && splitter_->restoreState(state);
        LayoutUtils::configureVerticalSplitter(splitter_,
            QStringLiteral("上下拖动，调整库位分布与业务流水高度"));
        const auto sizes = splitter_->sizes();
        if (!restored || sizes.at(0) <= 0 || sizes.at(1) <= 0) {
            const int height = std::max(0, splitter_->height() - splitter_->handleWidth());
            splitter_->setSizes({height / 3, height - height / 3});
        }
    });
}

Result<bool> BatchTrackingPage::refresh()
{
    const EntityId selected=selector_->currentData().toLongLong(); const auto result=service_.historicalBatches(); if(!result.ok()){showError(result.error());return Result<bool>::failure(result.error());}
    selector_->blockSignals(true);
    selector_->clear();
    for (const auto &batch : result.value()) {
        const QString text = QStringLiteral("%1 / %2 / 物料型号：%3")
            .arg(batch.batchNo, batch.typeCode,
                 batch.materialModel.isEmpty() ? QStringLiteral("未填写") : batch.materialModel);
        selector_->addItem(text, batch.batchId);
        selector_->setItemData(selector_->count() - 1, text, Qt::ToolTipRole);
    }
    selector_->blockSignals(false);
    if(selected){const auto selection=selectBatch(selected);if(!selection.ok())return selection;} else if(selector_->count())return loadSelected(); else{summary_->clear();distribution_->setRowCount(0);timeline_->setRowCount(0);error_->clear();} return Result<bool>::success(true);
}
Result<bool> BatchTrackingPage::selectBatch(EntityId batchId)
{
    const int index=selector_->findData(batchId); if(index<0){ServiceError error{QStringLiteral("not_found"),QStringLiteral("批次不存在"),QStringLiteral("batchId")};showError(error);return Result<bool>::failure(error);} const QSignalBlocker blocker(selector_); selector_->setCurrentIndex(index); return loadSelected();
}
Result<bool> BatchTrackingPage::loadSelected()
{
    selector_->setToolTip(selector_->currentText());
    const EntityId id=selector_->currentData().toLongLong(); if(!id)return Result<bool>::success(true);
    const auto distribution=service_.batchLocationBalances(id); if(!distribution.ok()){showError(distribution.error());return Result<bool>::failure(distribution.error());}
    const auto timeline=service_.batchTimeline(id); if(!timeline.ok()){showError(timeline.error());return Result<bool>::failure(timeline.error());}
    bool hasNegative = false;
    distribution_->setRowCount(distribution.value().size());
    for (int row = 0; row < distribution.value().size(); ++row) {
        const auto &value = distribution.value().at(row);
        setCells(distribution_, row, {value.locationName, QString::number(value.quantity)});
        if (value.quantity < 0) {
            hasNegative = true;
            StatusPresentation::apply(distribution_->item(row, 1), StatusPresentation::Tone::Attention);
        }
    }
    timeline_->setRowCount(timeline.value().size());
    for (int row = 0; row < timeline.value().size(); ++row) {
        const auto &v = timeline.value().at(row);
        setCells(timeline_, row, {v.businessAt.toString(QStringLiteral("yyyy-MM-dd HH:mm")),
            v.sourceDocument, v.direction, QString::number(v.quantityDelta), v.locationName,
            v.modelCode.isEmpty() ? QStringLiteral("未细分") : v.modelCode,
            StatusPresentation::documentText(v.documentStatus),
            v.materialModel.isEmpty() ? QStringLiteral("未填写") : v.materialModel});
        StatusPresentation::apply(timeline_->item(row, 6), StatusPresentation::tone(v.documentStatus));
        StatusPresentation::apply(timeline_->item(row, 2), v.isReversal
            ? StatusPresentation::Tone::Error
            : v.quantityDelta > 0 ? StatusPresentation::Tone::Success : StatusPresentation::Tone::Active);
    }
    LayoutUtils::fitTableColumns(distribution_);
    LayoutUtils::fitTableColumns(timeline_);
    if (!timeline.value().isEmpty()) {
        const auto &v = timeline.value().front();
        const auto remaining = service_.batchRemaining(id);
        if (!remaining.ok()) {
            showError(remaining.error());
            return Result<bool>::failure(remaining.error());
        }
        summary_->setText(QStringLiteral("%1 / %2 / %3 / 物料型号：%4 / %5")
            .arg(v.batchNo, v.categoryName, v.typeCode,
                 v.materialModel.isEmpty() ? QStringLiteral("未填写") : v.materialModel)
            .arg(remaining.value()) + (hasNegative
                ? QStringLiteral(" / 存在负库存，需核对或补录") : QString()));
    }
    if (hasNegative)
        StatusPresentation::apply(summary_, StatusPresentation::Tone::Attention);
    else
        summary_->setStyleSheet({});
    error_->clear();
    return Result<bool>::success(true);
}
void BatchTrackingPage::showError(const ServiceError &error){error_->setText(error.message);}
