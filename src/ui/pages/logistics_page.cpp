#include "logistics_page.h"

#include "services/batch_workflow_service.h"
#include "services/inventory_query_service.h"
#include "services/logistics_service.h"
#include "services/master_data_service.h"
#include "ui/dialogs/logistics_dialog.h"
#include "ui/widgets/status_presentation.h"
#include "ui/widgets/layout_utils.h"

#include <QComboBox>
#include <QDateTime>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVariant>
#include <QVBoxLayout>

#include <utility>

namespace {
class BusinessDateItem final : public QTableWidgetItem {
public:
    BusinessDateItem(const QString &text, const QDateTime &value)
        : QTableWidgetItem(text)
    {
        setData(Qt::UserRole, value);
    }

    bool operator<(const QTableWidgetItem &other) const override
    {
        return data(Qt::UserRole).toDateTime() < other.data(Qt::UserRole).toDateTime();
    }
};
}

LogisticsPage::LogisticsPage(MasterDataService &masterData, InventoryQueryService &queries,
                             LogisticsService &service, DocumentType type, QWidget *parent,
                             BatchWorkflowService *workflow)
    : QWidget(parent), masterData_(masterData), queries_(queries), service_(service), type_(type),
      workflow_(workflow)
{
    setObjectName(type == DocumentType::Return ? QStringLiteral("returnPage")
        : type == DocumentType::Transfer ? QStringLiteral("transferPage") : QStringLiteral("outboundPage"));
    auto *layout = new QVBoxLayout(this);
    workflowContext_ = new QLabel(this);
    workflowContext_->setObjectName(QStringLiteral("workflowContext"));
    workflowContext_->setWordWrap(true);
    const QString stepText = type_ == DocumentType::Transfer
        ? QStringLiteral("步骤 03 / 06：公司调拨到工厂")
        : type_ == DocumentType::Outbound ? QStringLiteral("步骤 04 / 06：工厂出库到 Amazon")
                                          : QStringLiteral("步骤 05 / 06：工厂退回公司（可选）");
    workflowContext_->setText(stepText + QStringLiteral("\n前置：请先选择当前批次\n当前操作：查看或新建业务单据"));
    layout->addWidget(workflowContext_);
    auto *actions = new QHBoxLayout;
    const auto button = [this, actions](const QString &text, const QString &name) {
        auto *value = new QPushButton(text, this); value->setObjectName(name); actions->addWidget(value); return value;
    };
    auto *create = button(QStringLiteral("新建"), QStringLiteral("createButton"));
    edit_ = button(QStringLiteral("编辑"), QStringLiteral("editButton"));
    view_ = button(QStringLiteral("查看"), QStringLiteral("viewButton"));
    cancel_ = button(QStringLiteral("作废"), QStringLiteral("cancelButton"));
    QStringList headers{QStringLiteral("序号"), QStringLiteral("单号"),
        QStringLiteral("业务日期"), QStringLiteral("状态"), QStringLiteral("来源")};
    if (type_ == DocumentType::Transfer)
        headers.append(QStringLiteral("接收工厂"));
    else if (type_ == DocumentType::Outbound)
        headers.append(QStringLiteral("出库确认人"));
    if (type_ == DocumentType::Transfer || type_ == DocumentType::Outbound) {
        headers.append(type_ == DocumentType::Transfer ? QStringLiteral("调拨型号")
                                                       : QStringLiteral("出库型号"));
        headers.append(QStringLiteral("数量"));
    }
    layout->addLayout(actions);
    if (type_ == DocumentType::Outbound) {
        auto *filters = new QHBoxLayout;
        sourceFilter_ = new QComboBox(this);
        sourceFilter_->setObjectName(QStringLiteral("sourceFilter"));
        sourceFilter_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        sourceFilter_->addItem(QStringLiteral("全部来源"), QVariant::fromValue(EntityId(0)));
        filters->addWidget(new QLabel(QStringLiteral("来源"), this));
        filters->addWidget(sourceFilter_);
        filters->addStretch();
        layout->addLayout(filters);
        connect(sourceFilter_, &QComboBox::currentIndexChanged, this, &LogisticsPage::applyFilters);
    }
    table_ = new QTableWidget(0, headers.size(), this);
    table_->setObjectName(QStringLiteral("documents"));
    table_->setHorizontalHeaderLabels(headers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    if (type_ == DocumentType::Transfer || type_ == DocumentType::Outbound) {
        table_->setWordWrap(false);
        table_->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    }
    if (type_ == DocumentType::Outbound) {
        auto *header = table_->horizontalHeader();
        header->setSectionsClickable(true);
        header->setSortIndicatorShown(false);
        connect(header, &QHeaderView::sectionClicked, this, [this, header](int column) {
            if (column != 0 && column != 1 && column != 2 && column != 4) {
                header->setSortIndicator(sortColumn_, sortOrder_);
                header->setSortIndicatorShown(sortColumn_ >= 0);
                return;
            }
            sortOrder_ = sortColumn_ == column && sortOrder_ == Qt::AscendingOrder
                ? Qt::DescendingOrder : Qt::AscendingOrder;
            sortColumn_ = column;
            const QSignalBlocker blocker(table_);
            table_->sortItems(sortColumn_, sortOrder_);
            header->setSortIndicatorShown(true);
            applyFilters();
        });
    }
    layout->addWidget(table_);
    error_ = new QLabel(this); error_->setObjectName(QStringLiteral("errorLabel"));
    error_->setStyleSheet(QStringLiteral("color:#b00020")); error_->setWordWrap(true); layout->addWidget(error_);
    connect(create, &QPushButton::clicked, this, [this] {
        emit createRequested(type_);
        openCreateDialog();
    });
    connect(edit_, &QPushButton::clicked, this, &LogisticsPage::editSelected);
    connect(view_, &QPushButton::clicked, this, &LogisticsPage::viewSelected);
    connect(cancel_, &QPushButton::clicked, this, &LogisticsPage::cancelSelected);
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        updateActions();
        if (selectedId() <= 0 || (currentBatchId_ == 0 &&
            (type_ == DocumentType::Transfer || type_ == DocumentType::Outbound)))
            return;
        const auto batchIds = table_->item(table_->currentRow(), 0)->data(Qt::UserRole + 2).toList();
        if (batchIds.isEmpty() || batchIds.contains(QVariant::fromValue(currentBatchId_)))
            return;
        const EntityId batchId = batchIds.first().toLongLong();
        if (batchId != currentBatchId_) {
            currentBatchId_ = batchId;
            emit batchSelected(batchId);
        }
    });
    refresh();
}

Result<bool> LogisticsPage::refresh()
{
    const auto result = service_.list(type_);
    if (!result.ok()) { setError(result.error()); return Result<bool>::failure(lastError_); }
    const EntityId selectedDocumentId =
        type_ == DocumentType::Transfer || type_ == DocumentType::Outbound ? selectedId() : 0;
    const QSignalBlocker blocker(table_);
    setError(); table_->clearContents();
    table_->setCurrentCell(-1, -1);
    table_->setRowCount(static_cast<int>(result.value().size()));
    for (int row = 0; row < result.value().size(); ++row) {
        const auto loaded = service_.byId(result.value().at(row).id);
        if (!loaded.ok()) {
            setError(loaded.error());
            table_->setRowCount(0);
            updateActions();
            return Result<bool>::failure(loaded.error());
        }
        const auto &document = loaded.value();
        QStringList cells{QString::number(row + 1), document.documentNo,
            document.businessAt.toString(QStringLiteral("yyyy-MM-dd HH:mm")), StatusPresentation::documentText(document.status),
            document.sourceNameSnapshot};
        if (type_ == DocumentType::Transfer)
            cells.append(document.destinationNameSnapshot);
        else if (type_ == DocumentType::Outbound)
            cells.append(document.outboundConfirmedByName.isEmpty()
                ? QStringLiteral("未填写") : document.outboundConfirmedByName);
        QVariantList batchIds;
        QStringList models;
        QStringList quantities;
        for (const auto &line : document.lines) {
            batchIds.append(QVariant::fromValue(line.batchId));
            models.append(line.modelCodeSnapshot.isEmpty()
                ? QStringLiteral("未细分 / 原材料") : line.modelCodeSnapshot);
            quantities.append(QString::number(line.quantity));
        }
        if (type_ == DocumentType::Transfer || type_ == DocumentType::Outbound) {
            cells.append(models.join(QLatin1Char('\n')));
            cells.append(quantities.join(QLatin1Char('\n')));
        }
        for (int column = 0; column < cells.size(); ++column) {
            QTableWidgetItem *item;
            if (column == 0) {
                item = new QTableWidgetItem;
                item->setData(Qt::DisplayRole, row + 1);
            } else if (type_ == DocumentType::Outbound && column == 2) {
                item = new BusinessDateItem(cells.at(column), document.businessAt);
            } else {
                item = new QTableWidgetItem(cells.at(column));
            }
            if (column >= 6)
                item->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);
            table_->setItem(row, column, item);
        }
        table_->item(row, 4)->setData(Qt::UserRole, document.sourceLocationId);
        StatusPresentation::apply(table_->item(row, 3), StatusPresentation::tone(document.status));
        table_->item(row, 0)->setData(Qt::UserRole, QVariant::fromValue(document.status));
        table_->item(row, 0)->setData(Qt::UserRole + 1, document.id);
        table_->item(row, 0)->setData(Qt::UserRole + 2, batchIds);
        if (type_ == DocumentType::Transfer || type_ == DocumentType::Outbound) {
            table_->item(row, 6)->setData(Qt::UserRole, models);
            table_->item(row, 7)->setData(Qt::UserRole, quantities);
        }
    }
    if (sourceFilter_) {
        const EntityId sourceId = sourceFilter_->currentData().toLongLong();
        const QSignalBlocker filterBlocker(sourceFilter_);
        sourceFilter_->clear();
        sourceFilter_->addItem(QStringLiteral("全部来源"), QVariant::fromValue(EntityId(0)));
        QSet<EntityId> sources;
        for (const auto &document : result.value()) {
            if (sources.contains(document.sourceLocationId))
                continue;
            sources.insert(document.sourceLocationId);
            sourceFilter_->addItem(document.sourceNameSnapshot, QVariant::fromValue(document.sourceLocationId));
        }
        const int index = sourceFilter_->findData(QVariant::fromValue(sourceId));
        sourceFilter_->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (sortColumn_ >= 0)
        table_->sortItems(sortColumn_, sortOrder_);
    applyFilters();
    bool selectionRestored = false;
    if (selectedDocumentId > 0) {
        for (int row = 0; row < table_->rowCount(); ++row) {
            if (!table_->isRowHidden(row) &&
                table_->item(row, 0)->data(Qt::UserRole + 1).toLongLong() == selectedDocumentId) {
                table_->selectRow(row);
                selectionRestored = true;
                break;
            }
        }
    }
    if (!selectionRestored && currentBatchId_ > 0)
        selectBatch(currentBatchId_);
    else
        updateActions();
    LayoutUtils::fitTableColumns(table_);
    return Result<bool>::success(true);
}

void LogisticsPage::applyFilters()
{
    if (type_ != DocumentType::Transfer && type_ != DocumentType::Outbound)
        return;
    const QSignalBlocker blocker(table_);
    const EntityId sourceId = sourceFilter_ ? sourceFilter_->currentData().toLongLong() : 0;
    for (int row = 0; row < table_->rowCount(); ++row) {
        const auto batchIds = table_->item(row, 0)->data(Qt::UserRole + 2).toList();
        auto *modelItem = table_->item(row, 6);
        auto *quantityItem = table_->item(row, 7);
        const auto models = modelItem->data(Qt::UserRole).toStringList();
        const auto quantities = quantityItem->data(Qt::UserRole).toStringList();
        QStringList visibleModels;
        QStringList visibleQuantities;
        for (int index = 0; index < batchIds.size(); ++index) {
            if (currentBatchId_ > 0 && batchIds.at(index).toLongLong() != currentBatchId_)
                continue;
            visibleModels.append(models.at(index));
            visibleQuantities.append(quantities.at(index));
        }
        modelItem->setText(visibleModels.join(QLatin1Char('\n')));
        quantityItem->setText(visibleQuantities.join(QLatin1Char('\n')));
        modelItem->setToolTip(modelItem->text());
        quantityItem->setToolTip(quantityItem->text());
        const bool matchesBatch = currentBatchId_ <= 0 || !visibleModels.isEmpty();
        const bool matchesSource = sourceId <= 0 ||
            table_->item(row, 4)->data(Qt::UserRole).toLongLong() == sourceId;
        table_->setRowHidden(row, !matchesBatch || !matchesSource);
    }
    if (table_->currentRow() >= 0 && table_->isRowHidden(table_->currentRow())) {
        table_->clearSelection();
        table_->setCurrentCell(-1, -1);
    }
    updateActions();
}

void LogisticsPage::setCurrentBatch(EntityId batchId)
{
    currentBatchId_ = batchId;
    if (type_ != DocumentType::Transfer && type_ != DocumentType::Outbound) {
        selectBatch(batchId);
        return;
    }
    applyFilters();
    if (batchId > 0)
        selectBatch(batchId);
    else
        setError();
    LayoutUtils::fitTableColumns(table_);
}

EntityId LogisticsPage::currentBatchId() const { return currentBatchId_; }

Result<bool> LogisticsPage::selectBatch(EntityId batchId)
{
    int selectedRow = -1;
    // 切换批次或刷新时，保留用户选中的可见调拨、出库单据。
    if ((type_ == DocumentType::Transfer || type_ == DocumentType::Outbound) && selectedId() > 0 &&
        table_->item(table_->currentRow(), 0)->data(Qt::UserRole + 2).toList()
            .contains(QVariant::fromValue(batchId)))
        selectedRow = table_->currentRow();
    for (int row = 0; selectedRow < 0 && row < table_->rowCount(); ++row) {
        if (table_->isRowHidden(row) || !table_->item(row, 0)->data(Qt::UserRole + 2).toList()
                 .contains(QVariant::fromValue(batchId)))
            continue;
        selectedRow = row;
        break;
    }
    const QSignalBlocker blocker(table_);
    if (selectedRow >= 0)
        table_->selectRow(selectedRow);
    else {
        table_->clearSelection();
        table_->setCurrentCell(-1, -1);
    }
    updateActions();
    setError();
    if (selectedRow >= 0)
        return Result<bool>::success(true);
    return Result<bool>::failure({QStringLiteral("not_found"),
                                  QStringLiteral("当前页面没有该批次的单据"),
                                  QStringLiteral("batchId")});
}

void LogisticsPage::setWorkflowMessage(const QString &message)
{
    QString detail = message;
    if (currentBatchId_ > 0 &&
        (type_ == DocumentType::Transfer || type_ == DocumentType::Outbound))
        detail += QStringLiteral("\n列表仅显示当前批次明细；查看、编辑和作废仍按整张单据处理");
    if (workflowContext_)
        workflowContext_->setText(QStringLiteral("%1\n%2")
                                  .arg(type_ == DocumentType::Transfer
                                           ? QStringLiteral("步骤 03 / 06：公司调拨到工厂")
                                           : type_ == DocumentType::Outbound
                                               ? QStringLiteral("步骤 04 / 06：工厂出库到 Amazon")
                                               : QStringLiteral("步骤 05 / 06：工厂退回公司（可选）"),
                                       detail));
}

ServiceError LogisticsPage::lastError() const { return lastError_; }
EntityId LogisticsPage::selectedId() const
{
    const int row = table_->currentRow();
    return row < 0 || table_->isRowHidden(row)
        ? 0 : table_->item(row, 0)->data(Qt::UserRole + 1).toLongLong();
}
void LogisticsPage::updateActions()
{
    const int row = table_->currentRow(); const bool selected = selectedId() > 0;
    edit_->setEnabled(selected);
    view_->setEnabled(selected);
    cancel_->setEnabled(selected && table_->item(row, 0)->data(Qt::UserRole).value<DocumentStatus>()
                                       == DocumentStatus::Confirmed);
}
void LogisticsPage::setConfirmationCallback(std::function<bool(const QString &)> callback) { confirmation_ = std::move(callback); }
bool LogisticsPage::confirmAction(const QString &text) const { return confirmation_ ? confirmation_(text) : QMessageBox::question(const_cast<LogisticsPage *>(this), QStringLiteral("确认"), text) == QMessageBox::Yes; }
void LogisticsPage::setError(const ServiceError &value) { lastError_ = value; error_->setText(value.message); }
WorkflowAction LogisticsPage::actionForType() const
{
    if (type_ == DocumentType::Transfer)
        return WorkflowAction::CreateTransfer;
    if (type_ == DocumentType::Return)
        return WorkflowAction::CreateReturn;
    return WorkflowAction::CreateOutbound;
}

Result<bool> LogisticsPage::openCreateDialog()
{
    if (workflow_ && currentBatchId_ > 0) {
        const auto allowed = workflow_->canStartAction(currentBatchId_, actionForType());
        if (!allowed.ok()) {
            setError(allowed.error());
            setWorkflowMessage(QStringLiteral("前置：%1").arg(allowed.error().message));
            return Result<bool>::failure(allowed.error());
        }
    }
    LogisticsDialog dialog(masterData_, queries_, service_, type_, this);
    const auto initialized = dialog.initialize(0, false, currentBatchId_);
    if (!initialized.ok()) { setError(initialized.error()); return initialized; }
    if (dialog.exec() == QDialog::Accepted) { setError(); emit dataChanged(); }
    return Result<bool>::success(true);
}
Result<bool> LogisticsPage::openEditDialog(EntityId id)
{
    LogisticsDialog dialog(masterData_, queries_, service_, type_, this);
    const auto initialized = dialog.initialize(id);
    if (!initialized.ok()) { setError(initialized.error()); return initialized; }
    if (dialog.exec() == QDialog::Accepted) { setError(); emit dataChanged(); }
    return Result<bool>::success(true);
}
Result<bool> LogisticsPage::openViewDialog(EntityId id)
{
    LogisticsDialog dialog(masterData_, queries_, service_, type_, this);
    const auto initialized = dialog.initialize(id, true);
    if (!initialized.ok()) { setError(initialized.error()); return initialized; }
    dialog.exec();
    return Result<bool>::success(true);
}
void LogisticsPage::editSelected()
{
    if (!selectedId()) return;
    emit editRequested(selectedId());
    openEditDialog(selectedId());
}
void LogisticsPage::viewSelected()
{
    if (!selectedId()) return;
    emit viewRequested(selectedId());
    openViewDialog(selectedId());
}
void LogisticsPage::cancelSelected()
{
    const QString confirmation = type_ == DocumentType::Transfer || type_ == DocumentType::Outbound
        ? QStringLiteral("确定作废整张单据并撤销其所有批次的库存影响？之后仍可编辑恢复。")
        : QStringLiteral("确定作废此记录并撤销其库存影响？之后仍可编辑恢复。");
    if (!selectedId() || !confirmAction(confirmation))
        return;
    const auto result = service_.cancel(selectedId());
    if (!result.ok())
        setError(result.error());
    else {
        setError();
        emit dataChanged();
    }
}
