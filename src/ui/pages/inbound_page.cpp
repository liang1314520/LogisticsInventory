#include "inbound_page.h"

#include "services/inbound_service.h"
#include "services/master_data_service.h"
#include "ui/dialogs/inbound_dialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVariant>
#include <QVBoxLayout>

#include <utility>

namespace {
QString statusText(DocumentStatus status)
{
    if (status == DocumentStatus::Confirmed) return QStringLiteral("已确认");
    if (status == DocumentStatus::Cancelled) return QStringLiteral("已取消");
    return QStringLiteral("草稿");
}
}

InboundPage::InboundPage(MasterDataService &masterData, InboundService &service, QWidget *parent)
    : QWidget(parent), masterData_(masterData), service_(service)
{
    setObjectName(QStringLiteral("inboundPage"));
    auto *layout = new QVBoxLayout(this); auto *actions = new QHBoxLayout;
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
    layout->addLayout(actions);
    table_ = new QTableWidget(0, 5, this); table_->setObjectName(QStringLiteral("documents"));
    table_->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("批次号"),
        QStringLiteral("业务日期"), QStringLiteral("状态"), QStringLiteral("数量")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(table_);
    error_ = new QLabel(this); error_->setObjectName(QStringLiteral("errorLabel"));
    error_->setStyleSheet(QStringLiteral("color:#b00020")); error_->setWordWrap(true); layout->addWidget(error_);
    connect(create, &QPushButton::clicked, this, [this] {
        emit createRequested();
        openCreateDialog();
    });
    connect(edit_, &QPushButton::clicked, this, &InboundPage::editSelected);
    connect(view_, &QPushButton::clicked, this, &InboundPage::viewSelected);
    connect(remove_, &QPushButton::clicked, this, &InboundPage::deleteSelected);
    connect(confirm_, &QPushButton::clicked, this, &InboundPage::confirmSelected);
    connect(cancel_, &QPushButton::clicked, this, &InboundPage::cancelSelected);
    connect(copy_, &QPushButton::clicked, this, &InboundPage::copySelected);
    connect(table_, &QTableWidget::itemSelectionChanged, this, &InboundPage::updateActions);
    refresh();
}

Result<bool> InboundPage::refresh()
{
    const auto result = service_.list();
    if (!result.ok()) { setError(result.error()); return Result<bool>::failure(lastError_); }
    setError(); table_->clearContents();
    table_->setRowCount(static_cast<int>(result.value().size()));
    for (int row = 0; row < result.value().size(); ++row) {
        const auto &batch = result.value().at(row);
        const QStringList cells{QString::number(batch.id), batch.batchNo,
            batch.businessAt.toString(QStringLiteral("yyyy-MM-dd HH:mm")), statusText(batch.status),
            QString::number(batch.quantity)};
        for (int column = 0; column < cells.size(); ++column) table_->setItem(row, column, new QTableWidgetItem(cells.at(column)));
        table_->item(row, 0)->setData(Qt::UserRole, QVariant::fromValue(batch.status));
    }
    updateActions(); return Result<bool>::success(true);
}

ServiceError InboundPage::lastError() const { return lastError_; }
EntityId InboundPage::selectedId() const { const int row = table_->currentRow(); return row < 0 ? 0 : table_->item(row, 0)->text().toLongLong(); }
void InboundPage::updateActions()
{
    const int row = table_->currentRow(); const bool selected = row >= 0;
    const auto status = selected ? table_->item(row, 0)->data(Qt::UserRole).value<DocumentStatus>() : DocumentStatus::Draft;
    edit_->setEnabled(selected && status == DocumentStatus::Draft); remove_->setEnabled(selected && status == DocumentStatus::Draft);
    confirm_->setEnabled(selected && status == DocumentStatus::Draft);
    view_->setEnabled(selected && status != DocumentStatus::Draft); cancel_->setEnabled(selected && status == DocumentStatus::Confirmed);
    copy_->setEnabled(selected && status == DocumentStatus::Cancelled);
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
void InboundPage::deleteSelected() { if (selectedId() && confirmAction(QStringLiteral("确定删除草稿？"))) { const auto value = service_.deleteDraft(selectedId()); if (!value.ok()) setError(value.error()); else { setError(); emit dataChanged(); } } }
void InboundPage::confirmSelected() { if (selectedId() && confirmAction(QStringLiteral("确定确认单据？"))) { const auto value = service_.confirm(selectedId()); if (!value.ok()) setError(value.error()); else { setError(); emit dataChanged(); } } }
void InboundPage::cancelSelected() { if (selectedId() && confirmAction(QStringLiteral("确定取消已确认单据？"))) { const auto value = service_.cancel(selectedId()); if (!value.ok()) setError(value.error()); else { setError(); emit dataChanged(); } } }
void InboundPage::copySelected() { if (selectedId()) { const auto value = service_.copyToDraft(selectedId()); if (!value.ok()) setError(value.error()); else { setError(); emit dataChanged(); } } }
