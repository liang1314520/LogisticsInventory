#include "logistics_page.h"

#include "services/batch_workflow_service.h"
#include "services/inventory_query_service.h"
#include "services/logistics_service.h"
#include "services/master_data_service.h"
#include "ui/dialogs/logistics_dialog.h"
#include "ui/widgets/status_presentation.h"
#include "ui/widgets/layout_utils.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVariant>
#include <QVBoxLayout>

#include <utility>

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
    remove_ = button(QStringLiteral("删除"), QStringLiteral("deleteButton"));
    confirm_ = button(QStringLiteral("确认"), QStringLiteral("confirmButton"));
    cancel_ = button(QStringLiteral("取消确认"), QStringLiteral("cancelButton"));
    copy_ = button(QStringLiteral("复制为草稿"), QStringLiteral("copyButton"));
    QStringList headers{QStringLiteral("序号"), QStringLiteral("单号"),
        QStringLiteral("业务日期"), QStringLiteral("状态"), QStringLiteral("来源")};
    if (type_ == DocumentType::Outbound)
        headers.append(QStringLiteral("出库确认人"));
    layout->addLayout(actions); table_ = new QTableWidget(0, headers.size(), this);
    table_->setObjectName(QStringLiteral("documents"));
    table_->setHorizontalHeaderLabels(headers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_);
    error_ = new QLabel(this); error_->setObjectName(QStringLiteral("errorLabel"));
    error_->setStyleSheet(QStringLiteral("color:#b00020")); error_->setWordWrap(true); layout->addWidget(error_);
    connect(create, &QPushButton::clicked, this, [this] {
        emit createRequested(type_);
        openCreateDialog();
    });
    connect(edit_, &QPushButton::clicked, this, &LogisticsPage::editSelected);
    connect(view_, &QPushButton::clicked, this, &LogisticsPage::viewSelected);
    connect(remove_, &QPushButton::clicked, this, &LogisticsPage::deleteSelected);
    connect(confirm_, &QPushButton::clicked, this, &LogisticsPage::confirmSelected);
    connect(cancel_, &QPushButton::clicked, this, &LogisticsPage::cancelSelected);
    connect(copy_, &QPushButton::clicked, this, &LogisticsPage::copySelected);
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        updateActions();
        if (selectedId() <= 0)
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
        if (type_ == DocumentType::Outbound)
            cells.append(document.outboundConfirmedByName.isEmpty()
                ? QStringLiteral("未填写") : document.outboundConfirmedByName);
        for (int column = 0; column < cells.size(); ++column) table_->setItem(row, column, new QTableWidgetItem(cells.at(column)));
        StatusPresentation::apply(table_->item(row, 3), StatusPresentation::tone(document.status));
        table_->item(row, 0)->setData(Qt::UserRole, QVariant::fromValue(document.status));
        table_->item(row, 0)->setData(Qt::UserRole + 1, document.id);
        QVariantList batchIds;
        for (const auto &line : document.lines)
            batchIds.append(QVariant::fromValue(line.batchId));
        table_->item(row, 0)->setData(Qt::UserRole + 2, batchIds);
    }
    if (currentBatchId_ > 0)
        selectBatch(currentBatchId_);
    else
        updateActions();
    LayoutUtils::fitTableColumns(table_);
    return Result<bool>::success(true);
}

void LogisticsPage::setCurrentBatch(EntityId batchId)
{
    currentBatchId_ = batchId;
    selectBatch(batchId);
}

EntityId LogisticsPage::currentBatchId() const { return currentBatchId_; }

Result<bool> LogisticsPage::selectBatch(EntityId batchId)
{
    int selectedRow = -1;
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (!table_->item(row, 0)->data(Qt::UserRole + 2).toList()
                 .contains(QVariant::fromValue(batchId)))
            continue;
        if (selectedRow < 0)
            selectedRow = row;
        const auto status = table_->item(row, 0)->data(Qt::UserRole).value<DocumentStatus>();
        if (status == DocumentStatus::Draft) {
            selectedRow = row;
            break;
        }
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
    if (workflowContext_)
        workflowContext_->setText(QStringLiteral("%1\n%2")
                                  .arg(type_ == DocumentType::Transfer
                                           ? QStringLiteral("步骤 03 / 06：公司调拨到工厂")
                                           : type_ == DocumentType::Outbound
                                               ? QStringLiteral("步骤 04 / 06：工厂出库到 Amazon")
                                               : QStringLiteral("步骤 05 / 06：工厂退回公司（可选）"),
                                       message));
}

ServiceError LogisticsPage::lastError() const { return lastError_; }
EntityId LogisticsPage::selectedId() const
{
    const int row = table_->currentRow();
    return row < 0 ? 0 : table_->item(row, 0)->data(Qt::UserRole + 1).toLongLong();
}
void LogisticsPage::updateActions()
{
    const int row = table_->currentRow(); const bool selected = row >= 0;
    const auto status = selected ? table_->item(row, 0)->data(Qt::UserRole).value<DocumentStatus>() : DocumentStatus::Draft;
    edit_->setEnabled(selected && status == DocumentStatus::Draft); remove_->setEnabled(selected && status == DocumentStatus::Draft);
    confirm_->setEnabled(selected && status == DocumentStatus::Draft);
    view_->setEnabled(selected && status != DocumentStatus::Draft); cancel_->setEnabled(selected && status == DocumentStatus::Confirmed);
    copy_->setEnabled(selected && status == DocumentStatus::Cancelled);
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
void LogisticsPage::deleteSelected() { if (selectedId() && confirmAction(QStringLiteral("确定删除草稿？"))) { const auto r = service_.deleteDraft(selectedId()); if (!r.ok()) setError(r.error()); else { setError(); emit dataChanged(); } } }
void LogisticsPage::confirmSelected() { if (selectedId() && confirmAction(QStringLiteral("确定确认单据？"))) { const auto r = service_.confirm(selectedId()); if (!r.ok()) setError(r.error()); else { setError(); emit dataChanged(); } } }
void LogisticsPage::cancelSelected() { if (selectedId() && confirmAction(QStringLiteral("确定取消已确认单据？"))) { const auto r = service_.cancel(selectedId()); if (!r.ok()) setError(r.error()); else { setError(); emit dataChanged(); } } }
void LogisticsPage::copySelected() { if (selectedId()) { const auto r = service_.copyToDraft(selectedId()); if (!r.ok()) setError(r.error()); else { setError(); emit dataChanged(); } } }
