#include "inbound_page.h"

#include "services/inbound_service.h"
#include "services/master_data_service.h"
#include "ui/dialogs/inbound_dialog.h"
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

InboundPage::InboundPage(MasterDataService &masterData, InboundService &service, QWidget *parent)
    : QWidget(parent), masterData_(masterData), service_(service)
{
    setObjectName(QStringLiteral("inboundPage"));
    auto *layout = new QVBoxLayout(this); auto *actions = new QHBoxLayout;
    workflowContext_ = new QLabel(this);
    workflowContext_->setObjectName(QStringLiteral("workflowContext"));
    workflowContext_->setWordWrap(true);
    workflowContext_->setText(QStringLiteral("步骤 02 / 06：公司入库\n前置：准备产品名称和公司库存库位\n当前操作：新建公司入库，保存即生效\n下一步：调拨到工厂，再从工厂出库到 Amazon"));
    layout->addWidget(workflowContext_);
    const auto button = [this, actions](const QString &text, const QString &name) {
        auto *value = new QPushButton(text, this); value->setObjectName(name); actions->addWidget(value); return value;
    };
    auto *create = button(QStringLiteral("新建"), QStringLiteral("createButton"));
    edit_ = button(QStringLiteral("编辑"), QStringLiteral("editButton"));
    view_ = button(QStringLiteral("查看"), QStringLiteral("viewButton"));
    cancel_ = button(QStringLiteral("作废"), QStringLiteral("cancelButton"));
    layout->addLayout(actions);
    table_ = new QTableWidget(0, 8, this); table_->setObjectName(QStringLiteral("documents"));
    table_->setHorizontalHeaderLabels({QStringLiteral("序号"), QStringLiteral("批次号"),
        QStringLiteral("产品名称"), QStringLiteral("业务日期"), QStringLiteral("状态"), QStringLiteral("数量"),
        QStringLiteral("物料型号"), QStringLiteral("入库签收人")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_);
    error_ = new QLabel(this); error_->setObjectName(QStringLiteral("errorLabel"));
    error_->setStyleSheet(QStringLiteral("color:#b00020")); error_->setWordWrap(true); layout->addWidget(error_);
    connect(create, &QPushButton::clicked, this, [this] {
        emit createRequested();
        openCreateDialog();
    });
    connect(edit_, &QPushButton::clicked, this, &InboundPage::editSelected);
    connect(view_, &QPushButton::clicked, this, &InboundPage::viewSelected);
    connect(cancel_, &QPushButton::clicked, this, &InboundPage::cancelSelected);
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        updateActions();
        const EntityId id = selectedId();
        if (id > 0 && id != currentBatchId_) {
            currentBatchId_ = id;
            emit batchSelected(id);
        }
    });
    refresh();
}

Result<bool> InboundPage::refresh()
{
    const auto result = service_.list();
    if (!result.ok()) { setError(result.error()); return Result<bool>::failure(lastError_); }
    const QSignalBlocker blocker(table_);
    setError(); table_->clearContents();
    table_->setCurrentCell(-1, -1);
    table_->setRowCount(static_cast<int>(result.value().size()));
    for (int row = 0; row < result.value().size(); ++row) {
        const auto &batch = result.value().at(row);
        const QStringList cells{QString::number(row + 1), batch.batchNo, batch.typeCodeSnapshot,
            batch.businessAt.toString(QStringLiteral("yyyy-MM-dd HH:mm")), StatusPresentation::documentText(batch.status),
            QString::number(batch.quantity),
            batch.materialModel.isEmpty() ? QStringLiteral("未填写") : batch.materialModel,
            batch.receivedByName.isEmpty() ? QStringLiteral("未填写") : batch.receivedByName};
        for (int column = 0; column < cells.size(); ++column) table_->setItem(row, column, new QTableWidgetItem(cells.at(column)));
        StatusPresentation::apply(table_->item(row, 4), StatusPresentation::tone(batch.status));
        table_->item(row, 0)->setData(Qt::UserRole, QVariant::fromValue(batch.status));
        table_->item(row, 0)->setData(Qt::UserRole + 1, batch.id);
    }
    if (currentBatchId_ > 0)
        selectBatch(currentBatchId_);
    else
        updateActions();
    LayoutUtils::fitTableColumns(table_);
    return Result<bool>::success(true);
}

void InboundPage::setCurrentBatch(EntityId batchId)
{
    currentBatchId_ = batchId;
    selectBatch(batchId);
}

EntityId InboundPage::currentBatchId() const { return currentBatchId_; }

Result<bool> InboundPage::selectBatch(EntityId batchId)
{
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (table_->item(row, 0)->data(Qt::UserRole + 1).toLongLong() != batchId)
            continue;
        {
            const QSignalBlocker blocker(table_);
            table_->selectRow(row);
        }
        updateActions();
        setError();
        return Result<bool>::success(true);
    }
    const QSignalBlocker blocker(table_);
    table_->clearSelection();
    table_->setCurrentCell(-1, -1);
    updateActions();
    setError();
    return Result<bool>::failure({QStringLiteral("not_found"), QStringLiteral("入库记录不存在"),
                                  QStringLiteral("batchId")});
}

void InboundPage::setWorkflowMessage(const QString &message)
{
    if (workflowContext_)
        workflowContext_->setText(QStringLiteral("步骤 02 / 06：公司入库\n%1").arg(message));
}

ServiceError InboundPage::lastError() const { return lastError_; }
EntityId InboundPage::selectedId() const
{
    const int row = table_->currentRow();
    return row < 0 ? 0 : table_->item(row, 0)->data(Qt::UserRole + 1).toLongLong();
}
void InboundPage::updateActions()
{
    const int row = table_->currentRow(); const bool selected = row >= 0;
    edit_->setEnabled(selected);
    view_->setEnabled(selected);
    cancel_->setEnabled(selected && table_->item(row, 0)->data(Qt::UserRole).value<DocumentStatus>()
                                       == DocumentStatus::Confirmed);
}
void InboundPage::setConfirmationCallback(std::function<bool(const QString &)> callback) { confirmation_ = std::move(callback); }
bool InboundPage::confirmAction(const QString &text) const { return confirmation_ ? confirmation_(text) : QMessageBox::question(const_cast<InboundPage *>(this), QStringLiteral("确认"), text) == QMessageBox::Yes; }
void InboundPage::setError(const ServiceError &value) { lastError_ = value; error_->setText(value.message); }
Result<bool> InboundPage::openCreateDialog()
{
    InboundDialog dialog(masterData_, service_, this);
    const auto initialized = dialog.initialize();
    if (!initialized.ok()) { setError(initialized.error()); return initialized; }
    if (dialog.exec() == QDialog::Accepted) { setError(); emit dataChanged(); }
    return Result<bool>::success(true);
}
Result<bool> InboundPage::openEditDialog(EntityId id)
{
    InboundDialog dialog(masterData_, service_, this);
    const auto initialized = dialog.initialize(id);
    if (!initialized.ok()) { setError(initialized.error()); return initialized; }
    if (dialog.exec() == QDialog::Accepted) { setError(); emit dataChanged(); }
    return Result<bool>::success(true);
}
Result<bool> InboundPage::openViewDialog(EntityId id)
{
    InboundDialog dialog(masterData_, service_, this);
    const auto initialized = dialog.initialize(id, true);
    if (!initialized.ok()) { setError(initialized.error()); return initialized; }
    dialog.exec();
    return Result<bool>::success(true);
}
void InboundPage::editSelected()
{
    if (!selectedId()) return;
    emit editRequested(selectedId());
    openEditDialog(selectedId());
}
void InboundPage::viewSelected()
{
    if (!selectedId()) return;
    emit viewRequested(selectedId());
    openViewDialog(selectedId());
}
void InboundPage::cancelSelected()
{
    if (!selectedId() || !confirmAction(QStringLiteral("确定作废此记录并撤销其库存影响？之后仍可编辑恢复。")))
        return;
    const auto value = service_.cancel(selectedId());
    if (!value.ok())
        setError(value.error());
    else {
        setError();
        emit dataChanged();
    }
}
