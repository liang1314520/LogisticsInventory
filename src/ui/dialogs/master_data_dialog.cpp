#include "master_data_dialog.h"

#include "services/master_data_service.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

MasterDataDialog::MasterDataDialog(MasterDataService &service, Entity entity, QWidget *parent)
    : QDialog(parent), service_(service), entity_(entity)
{
    setObjectName(QStringLiteral("masterDataDialog"));
    setModal(true);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    if (entity_ == Entity::Category || entity_ == Entity::Location) {
        name_ = new QLineEdit(this);
        name_->setObjectName(QStringLiteral("name"));
        form->addRow(QStringLiteral("名称"), name_);
    }
    if (entity_ == Entity::ProductType) {
        category_ = new QComboBox(this);
        category_->setObjectName(QStringLiteral("categoryId"));
        code_ = new QLineEdit(this);
        code_->setObjectName(QStringLiteral("code"));
        details_ = new QLineEdit(this);
        details_->setObjectName(QStringLiteral("details"));
        form->addRow(QStringLiteral("分类"), category_);
        form->addRow(QStringLiteral("类型编码"), code_);
        form->addRow(QStringLiteral("说明"), details_);
    } else if (entity_ == Entity::Location) {
        locationType_ = new QComboBox(this);
        locationType_->setObjectName(QStringLiteral("locationType"));
        const QStringList types{QStringLiteral("公司"), QStringLiteral("工厂"),
                                QStringLiteral("客户"), QStringLiteral("项目现场"),
                                QStringLiteral("其他")};
        for (int index = 0; index < types.size(); ++index)
            locationType_->addItem(types.at(index), index);
        inventory_ = new QCheckBox(this);
        inventory_->setObjectName(QStringLiteral("countsAsInventory"));
        form->addRow(QStringLiteral("类型"), locationType_);
        form->addRow(QStringLiteral("计入库存"), inventory_);
    }
    layout->addLayout(form);

    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("errorLabel"));
    error_->setStyleSheet(QStringLiteral("color:#b00020"));
    layout->addWidget(error_);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    save_ = buttons->button(QDialogButtonBox::Save);
    connect(save_, &QPushButton::clicked, this, [this] {
        if (save())
            accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

Result<bool> MasterDataDialog::initialize(EntityId id)
{
    id_ = id;
    if (entity_ == Entity::ProductType) {
        const auto categories = service_.allCategoriesResult();
        if (!categories.ok()) {
            showError(categories.error());
            return Result<bool>::failure(categories.error());
        }
        category_->clear();
        for (const auto &category : categories.value())
            category_->addItem(category.name + (category.enabled ? QString() : QStringLiteral(" (已停用)")),
                               category.id);
    }
    if (!id)
        return Result<bool>::success(true);

    if (entity_ == Entity::Category) {
        const auto value = service_.categoryById(id);
        if (!value.ok()) {
            showError(value.error());
            return Result<bool>::failure(value.error());
        }
        name_->setText(value.value().name);
    } else if (entity_ == Entity::ProductType) {
        const auto value = service_.productTypeById(id);
        if (!value.ok()) {
            showError(value.error());
            return Result<bool>::failure(value.error());
        }
        code_->setText(value.value().typeCode);
        details_->setText(value.value().description);
        category_->setCurrentIndex(category_->findData(value.value().categoryId));
    } else {
        const auto value = service_.locationById(id);
        if (!value.ok()) {
            showError(value.error());
            return Result<bool>::failure(value.error());
        }
        name_->setText(value.value().name);
        locationType_->setCurrentIndex(static_cast<int>(value.value().type));
        inventory_->setChecked(value.value().countsAsInventory);
    }
    return Result<bool>::success(true);
}

bool MasterDataDialog::save()
{
    Result<bool> result = Result<bool>::success(true);
    if (entity_ == Entity::Category) {
        const auto value = id_ ? service_.updateCategory(id_, name_->text())
                               : service_.createCategory(name_->text());
        if (!value.ok())
            result = Result<bool>::failure(value.error());
    } else if (entity_ == Entity::ProductType) {
        const auto value = id_
            ? service_.updateProductType(id_, category_->currentData().toLongLong(),
                                         code_->text(), details_->text())
            : service_.createProductType(category_->currentData().toLongLong(),
                                         code_->text(), details_->text());
        if (!value.ok())
            result = Result<bool>::failure(value.error());
    } else {
        const auto type = static_cast<LocationType>(locationType_->currentData().toInt());
        const auto value = id_ ? service_.updateLocation(id_, name_->text(), type,
                                                         inventory_->isChecked())
                               : service_.createLocation(name_->text(), type,
                                                         inventory_->isChecked());
        if (!value.ok())
            result = Result<bool>::failure(value.error());
    }
    if (!result.ok()) {
        showError(result.error());
        return false;
    }
    error_->clear();
    return true;
}

QWidget *MasterDataDialog::controlForField(const QString &field) const
{
    if (field == QStringLiteral("name"))
        return name_;
    if (field == QStringLiteral("typeCode") || field == QStringLiteral("code"))
        return code_;
    if (field == QStringLiteral("categoryId"))
        return category_;
    return nullptr;
}

void MasterDataDialog::showError(const ServiceError &error)
{
    error_->setText(error.message);
    if (auto *control = controlForField(error.field))
        control->setFocus();
}
