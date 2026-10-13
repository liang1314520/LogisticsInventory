#include "logistics_page.h"

#include "services/inventory_query_service.h"
#include "services/logistics_service.h"
#include "services/master_data_service.h"
#include "ui/dialogs/logistics_dialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVariant>
#include <QVBoxLayout>

#include <utility>

namespace {
QString logisticsStatus(DocumentStatus status)
{
    if (status == DocumentStatus::Confirmed) return QStringLiteral("已确认");
    if (status == DocumentStatus::Cancelled) return QStringLiteral("已取消");
    return QStringLiteral("草稿");
}
}

LogisticsPage::LogisticsPage(MasterDataService &masterData, InventoryQueryService &queries,
                             LogisticsService &service, DocumentType type, QWidget *parent)
    : QWidget(parent), masterData_(masterData), queries_(queries), service_(service), type_(type)
{
    setObjectName(type == DocumentType::Transfer ? QStringLiteral("transferPage") : QStringLiteral("outboundPage"));
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
    layout->addLayout(actions); table_ = new QTableWidget(0, 5, this);
    table_->setObjectName(QStringLiteral("documents"));
    table_->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("单号"),
        QStringLiteral("业务日期"), QStringLiteral("状态"), QStringLiteral("来源")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection);
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
    connect(table_, &QTableWidget::itemSelectionChanged, this, &LogisticsPage::updateActions);
    refresh();
}

Result<bool> LogisticsPage::refresh()
{
    const auto result = service_.list(type_);
    if (!result.ok()) { setError(result.error()); return Result<bool>::failure(lastError_); }
    setError(); table_->clearContents();
    table_->setRowCount(static_cast<int>(result.value().size()));
    for (int row = 0; row < result.value().size(); ++row) {
        const auto &document = result.value().at(row);
        const QStringList cells{QString::number(document.id), document.documentNo,
            document.businessAt.toString(QStringLiteral("yyyy-MM-dd HH:mm")), logisticsStatus(document.status),
            document.sourceNameSnapshot};
        for (int column = 0; column < cells.size(); ++column) table_->setItem(row, column, new QTableWidgetItem(cells.at(column)));
        table_->item(row, 0)->setData(Qt::UserRole, QVariant::fromValue(document.status));
    }
    updateActions(); return Result<bool>::success(true);
}
ServiceError LogisticsPage::lastError() const { return lastError_; }
EntityId LogisticsPage::selectedId() const { const int row = table_->currentRow(); return row < 0 ? 0 : table_->item(row, 0)->text().toLongLong(); }
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
Result<bool> LogisticsPage::openCreateDialog()
{
    LogisticsDialog dialog(masterData_, queries_, service_, type_, this);
    const auto initialized = dialog.initialize();
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
