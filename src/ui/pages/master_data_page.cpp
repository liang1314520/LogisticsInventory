#include "master_data_page.h"

#include "services/master_data_service.h"
#include "ui/dialogs/master_data_dialog.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <utility>

namespace {
QTableWidget *makeTable(const QString &name, const QStringList &headers, QWidget *parent)
{
    auto *table = new QTableWidget(0, headers.size(), parent);
    table->setObjectName(name);
    table->setHorizontalHeaderLabels(headers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    return table;
}

void setRow(QTableWidget *table, int row, EntityId id, const QStringList &values, bool enabled)
{
    for (int column = 0; column < values.size(); ++column)
        table->setItem(row, column, new QTableWidgetItem(values.at(column)));
    table->item(row, 0)->setData(Qt::UserRole, id);
    table->item(row, table->columnCount() - 1)->setData(Qt::UserRole, enabled);
}

QString state(bool enabled)
{
    return enabled ? QStringLiteral("启用") : QStringLiteral("停用");
}

QString locationType(LocationType type)
{
    switch (type) {
    case LocationType::Company:
        return QStringLiteral("公司");
    case LocationType::Factory:
        return QStringLiteral("工厂");
    case LocationType::Customer:
        return QStringLiteral("客户");
    case LocationType::ProjectSite:
        return QStringLiteral("项目现场");
    case LocationType::Other:
        return QStringLiteral("其他");
    }
    return QStringLiteral("其他");
}
}

MasterDataPage::MasterDataPage(MasterDataService &service, QWidget *parent)
    : QWidget(parent), service_(service)
{
    setObjectName(QStringLiteral("masterDataPage"));
    auto *layout = new QVBoxLayout(this);
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("masterDataTabs"));
    categories_ = makeTable(QStringLiteral("categoriesTable"),
                            {QStringLiteral("名称"), QStringLiteral("状态")}, tabs_);
    productTypes_ = makeTable(QStringLiteral("productTypesTable"),
                              {QStringLiteral("类型编码"), QStringLiteral("说明"),
                               QStringLiteral("分类ID"), QStringLiteral("状态")},
                              tabs_);
    locations_ = makeTable(QStringLiteral("locationsTable"),
                           {QStringLiteral("名称"), QStringLiteral("类型"),
                            QStringLiteral("计库存"), QStringLiteral("状态")},
                           tabs_);
    tabs_->addTab(categories_, QStringLiteral("分类"));
    tabs_->addTab(productTypes_, QStringLiteral("产品类型"));
    tabs_->addTab(locations_, QStringLiteral("库位"));
    layout->addWidget(tabs_);

    auto *actions = new QHBoxLayout;
    auto *add = new QPushButton(QStringLiteral("新建"), this);
    add->setObjectName(QStringLiteral("addButton"));
    auto *edit = new QPushButton(QStringLiteral("编辑"), this);
    edit->setObjectName(QStringLiteral("editButton"));
    toggle_ = new QPushButton(QStringLiteral("启用/停用"), this);
    toggle_->setObjectName(QStringLiteral("toggleButton"));
    auto *remove = new QPushButton(QStringLiteral("删除"), this);
    remove->setObjectName(QStringLiteral("deleteButton"));
    actions->addWidget(add);
    actions->addWidget(edit);
    actions->addWidget(toggle_);
    actions->addWidget(remove);
    actions->addStretch();
    layout->addLayout(actions);

    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("errorLabel"));
    error_->setStyleSheet(QStringLiteral("color:#b00020"));
    error_->setWordWrap(true);
    layout->addWidget(error_);

    connect(add, &QPushButton::clicked, this, &MasterDataPage::addSelectedType);
    connect(edit, &QPushButton::clicked, this, &MasterDataPage::editSelected);
    connect(toggle_, &QPushButton::clicked, this, &MasterDataPage::toggleSelected);
    connect(remove, &QPushButton::clicked, this, &MasterDataPage::deleteSelected);
    refresh();
}

Result<bool> MasterDataPage::refresh()
{
    const auto categories = service_.allCategoriesResult();
    if (!categories.ok()) {
        showError(categories.error());
        return Result<bool>::failure(categories.error());
    }
    const auto productTypes = service_.allProductTypesResult();
    if (!productTypes.ok()) {
        showError(productTypes.error());
        return Result<bool>::failure(productTypes.error());
    }
    const auto locations = service_.allLocationsResult();
    if (!locations.ok()) {
        showError(locations.error());
        return Result<bool>::failure(locations.error());
    }

    categories_->setRowCount(categories.value().size());
    for (int row = 0; row < categories.value().size(); ++row) {
        const auto &value = categories.value().at(row);
        setRow(categories_, row, value.id, {value.name, state(value.enabled)}, value.enabled);
    }
    productTypes_->setRowCount(productTypes.value().size());
    for (int row = 0; row < productTypes.value().size(); ++row) {
        const auto &value = productTypes.value().at(row);
        setRow(productTypes_, row, value.id,
               {value.typeCode, value.description, QString::number(value.categoryId),
                state(value.enabled)},
               value.enabled);
    }
    locations_->setRowCount(locations.value().size());
    for (int row = 0; row < locations.value().size(); ++row) {
        const auto &value = locations.value().at(row);
        setRow(locations_, row, value.id,
               {value.name, locationType(value.type),
                value.countsAsInventory ? QStringLiteral("是") : QStringLiteral("否"),
                state(value.enabled)},
               value.enabled);
    }
    error_->clear();
    return Result<bool>::success(true);
}

QTableWidget *MasterDataPage::currentTable() const
{
    switch (tabs_->currentIndex()) {
    case 0:
        return categories_;
    case 1:
        return productTypes_;
    case 2:
        return locations_;
    default:
        return nullptr;
    }
}

EntityId MasterDataPage::selectedId() const
{
    auto *table = currentTable();
    if (!table || table->currentRow() < 0 || !table->item(table->currentRow(), 0))
        return 0;
    return table->item(table->currentRow(), 0)->data(Qt::UserRole).toLongLong();
}

void MasterDataPage::addSelectedType()
{
    MasterDataDialog dialog(service_, static_cast<MasterDataDialog::Entity>(tabs_->currentIndex()),
                            this);
    const auto initialized = dialog.initialize();
    if (!initialized.ok()) {
        showError(initialized.error());
        return;
    }
    if (dialog.exec() == QDialog::Accepted)
        refresh();
}

void MasterDataPage::editSelected()
{
    const EntityId id = selectedId();
    if (!id)
        return;
    MasterDataDialog dialog(service_, static_cast<MasterDataDialog::Entity>(tabs_->currentIndex()),
                            this);
    const auto initialized = dialog.initialize(id);
    if (!initialized.ok()) {
        showError(initialized.error());
        return;
    }
    if (dialog.exec() == QDialog::Accepted)
        refresh();
}

void MasterDataPage::toggleSelected()
{
    const EntityId id = selectedId();
    auto *table = currentTable();
    if (!id || !table)
        return;
    const bool enabled = !table->item(table->currentRow(), table->columnCount() - 1)
                              ->data(Qt::UserRole)
                              .toBool();
    Result<bool> result = Result<bool>::success(true);
    switch (tabs_->currentIndex()) {
    case 0:
        result = service_.setCategoryEnabled(id, enabled);
        break;
    case 1:
        result = service_.setProductTypeEnabled(id, enabled);
        break;
    case 2:
        result = service_.setLocationEnabled(id, enabled);
        break;
    default:
        return;
    }
    if (!result.ok()) {
        showError(result.error());
        return;
    }
    refresh();
}

void MasterDataPage::showError(const ServiceError &error)
{
    error_->setText(error.message);
}

void MasterDataPage::setConfirmationCallback(std::function<bool(const QString &)> callback)
{
    confirmation_ = std::move(callback);
}

bool MasterDataPage::confirmAction(const QString &text) const
{
    if (confirmation_)
        return confirmation_(text);
    return QMessageBox::question(const_cast<MasterDataPage *>(this), QStringLiteral("确认删除"),
                                 text) == QMessageBox::Yes;
}

void MasterDataPage::deleteSelected()
{
    const EntityId id = selectedId();
    if (!id)
        return;
    const int tab = tabs_->currentIndex();
    const QStringList names{QStringLiteral("分类"), QStringLiteral("产品类型"),
                            QStringLiteral("库位")};
    if (tab < 0 || tab >= names.size())
        return;

    const auto references = tab == 0 ? service_.categoryReferenceCount(id)
        : tab == 1 ? service_.productTypeReferenceCount(id)
                   : service_.locationReferenceCount(id);
    if (!references.ok()) {
        showError(references.error());
        return;
    }

    const QString prompt = references.value() > 0
        ? QStringLiteral("该%1已被 %2 条业务记录引用。\n删除将同时清除相关单据、明细和库存流水，"
                         "且无法恢复。\n确定继续？")
              .arg(names.at(tab)).arg(references.value())
        : QStringLiteral("确定删除该%1？此操作无法恢复。").arg(names.at(tab));
    if (!confirmAction(prompt))
        return;

    const auto result = tab == 0 ? service_.deleteCategory(id)
        : tab == 1 ? service_.deleteProductType(id)
                   : service_.deleteLocation(id);
    if (!result.ok()) {
        showError(result.error());
        return;
    }
    refresh();
}
