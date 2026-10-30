#include "master_data_dialog.h"

#include "core/packaging.h"
#include "services/master_data_service.h"
#include "ui/widgets/layout_utils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

MasterDataDialog::MasterDataDialog(MasterDataService &service, Entity entity, QWidget *parent)
    : QDialog(parent), service_(service), entity_(entity)
{
    setObjectName(QStringLiteral("masterDataDialog"));
    setModal(true);
    setSizeGripEnabled(true);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    if (entity_ == Entity::Category || entity_ == Entity::Location || entity_ == Entity::Personnel) {
        name_ = new QLineEdit(this);
        name_->setObjectName(QStringLiteral("name"));
        form->addRow(entity_ == Entity::Personnel ? QStringLiteral("姓名") : QStringLiteral("名称"), name_);
        if (entity_ == Entity::Personnel)
            name_->setPlaceholderText(QStringLiteral("同名人员可填写：姓名（部门）"));
    }
    if (entity_ == Entity::ProductType || entity_ == Entity::ProductModel) {
        const bool model = entity_ == Entity::ProductModel;
        category_ = new QComboBox(this);
        category_->setObjectName(model ? QStringLiteral("productTypeId") : QStringLiteral("categoryId"));
        code_ = new QLineEdit(this);
        code_->setObjectName(QStringLiteral("code"));
        code_->setPlaceholderText(model ? QStringLiteral("例如：F25、P25")
                                        : QStringLiteral("例如：网关、保险箱"));
        if (model)
            code_->setToolTip(QStringLiteral("用于调拨和出库；入库物料型号（如 V40）在入库单中填写"));
        details_ = new QLineEdit(this);
        details_->setObjectName(QStringLiteral("details"));
        form->addRow(model ? QStringLiteral("所属产品") : QStringLiteral("分类"), category_);
        form->addRow(model ? QStringLiteral("调拨/出库型号") : QStringLiteral("产品名称"), code_);
        form->addRow(QStringLiteral("说明"), details_);
        if (model) {
            unitsPerBox_ = new QLineEdit(this);
            unitsPerBox_->setObjectName(QStringLiteral("unitsPerBox"));
            boxesPerCarton_ = new QLineEdit(this);
            boxesPerCarton_->setObjectName(QStringLiteral("boxesPerCarton"));
            for (auto *edit : {unitsPerBox_, boxesPerCarton_}) {
                edit->setPlaceholderText(QStringLiteral("留空或0表示未设置包装"));
                edit->setValidator(new QRegularExpressionValidator(
                    QRegularExpression(QStringLiteral("[0-9]{0,19}")), edit));
                connect(edit, &QLineEdit::textChanged, this, &MasterDataDialog::updatePackagingHint);
            }
            packagingHint_ = new QLabel(this);
            packagingHint_->setWordWrap(true);
            form->addRow(QStringLiteral("每盒个数"), unitsPerBox_);
            form->addRow(QStringLiteral("每箱盒数"), boxesPerCarton_);
            form->addRow(packagingHint_);
            packagingHint_->setText(QStringLiteral("未设置包装，仅按个录入。"));
        }
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
        inventory_->setChecked(true);
        inventory_->setEnabled(false);
        connect(locationType_, &QComboBox::currentIndexChanged, this, [this] {
            const auto type = static_cast<LocationType>(locationType_->currentData().toInt());
            const bool companyOrFactory = type == LocationType::Company || type == LocationType::Factory;
            inventory_->setEnabled(!companyOrFactory);
            inventory_->setChecked(companyOrFactory);
        });
        form->addRow(QStringLiteral("类型"), locationType_);
        form->addRow(QStringLiteral("计入库存"), inventory_);
        auto *hint = new QLabel(QStringLiteral(
            "公司和工厂都计库存：公司发工厂用调拨单，工厂发海外 Amazon 仓用出库单，"
            "工厂退公司用退回单（总库存不变）。Amazon 仓可选“其他”类型且不勾选计入库存。\n"
            "修改库位类型或计库存属性会影响关联库存归属及汇总，不会删除或转换已有记录。"), this);
        hint->setWordWrap(true);
        form->addRow(hint);
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
    for (auto *edit : {name_, code_}) {
        if (edit)
            connect(edit, &QLineEdit::textEdited, this, &MasterDataDialog::updateContentSize);
    }
    for (auto *combo : {category_, locationType_}) {
        if (combo)
            connect(combo, &QComboBox::activated, this, &MasterDataDialog::updateContentSize);
    }
}

Result<PackagingSpec> MasterDataDialog::packagingSpec() const
{
    const auto read = [](QLineEdit *edit, const QString &field) {
        if (edit->text().isEmpty())
            return Result<qint64>::success(0);
        bool ok = false;
        const qint64 value = edit->text().toLongLong(&ok);
        if (!ok || !edit->hasAcceptableInput())
            return Result<qint64>::failure({QStringLiteral("invalid_packaging"),
                QStringLiteral("包装规格必须为范围内的非负整数"), field});
        return Result<qint64>::success(value);
    };
    const auto units = read(unitsPerBox_, QStringLiteral("unitsPerBox"));
    if (!units.ok()) return Result<PackagingSpec>::failure(units.error());
    const auto boxes = read(boxesPerCarton_, QStringLiteral("boxesPerCarton"));
    if (!boxes.ok()) return Result<PackagingSpec>::failure(boxes.error());
    const PackagingSpec spec{units.value(), boxes.value()};
    const auto valid = validatePackagingSpec(spec);
    return valid.ok() ? Result<PackagingSpec>::success(spec)
                      : Result<PackagingSpec>::failure(valid.error());
}

void MasterDataDialog::updatePackagingHint()
{
    const auto spec = packagingSpec();
    if (!spec.ok()) {
        packagingHint_->setText(spec.error().message);
    } else if (spec.value().unitsPerBox == 0) {
        packagingHint_->setText(QStringLiteral("未设置包装，仅按个录入。"));
    } else {
        const auto total = calculatePackagingQuantity({1, 0, 0}, spec.value());
        packagingHint_->setText(QStringLiteral("1箱＝%1盒＝%2个")
            .arg(spec.value().boxesPerCarton).arg(total.value()));
    }
    updateContentSize();
}

void MasterDataDialog::updateContentSize()
{
    LayoutUtils::fitDialogToContents(this, QSize(640, 400));
}

void MasterDataDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    updateContentSize();
}

void MasterDataDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::FontChange)
        QTimer::singleShot(0, this, &MasterDataDialog::updateContentSize);
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
    if (entity_ == Entity::ProductModel) {
        const auto types = service_.allProductTypesResult();
        if (!types.ok()) {
            showError(types.error());
            return Result<bool>::failure(types.error());
        }
        category_->clear();
        for (const auto &type : types.value())
            category_->addItem(type.typeCode + (type.enabled ? QString() : QStringLiteral(" (已停用)")),
                               type.id);
    }
    if (!id) {
        updateContentSize();
        return Result<bool>::success(true);
    }

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
    } else if (entity_ == Entity::ProductModel) {
        const auto value = service_.productModelById(id);
        if (!value.ok()) {
            showError(value.error());
            return Result<bool>::failure(value.error());
        }
        code_->setText(value.value().modelCode);
        details_->setText(value.value().description);
        category_->setCurrentIndex(category_->findData(value.value().productTypeId));
        unitsPerBox_->setText(QString::number(value.value().packagingSpec.unitsPerBox));
        boxesPerCarton_->setText(QString::number(value.value().packagingSpec.boxesPerCarton));
    } else if (entity_ == Entity::Personnel) {
        const auto value = service_.personnelById(id);
        if (!value.ok()) {
            showError(value.error());
            return Result<bool>::failure(value.error());
        }
        name_->setText(value.value().name);
    } else {
        const auto value = service_.locationById(id);
        if (!value.ok()) {
            showError(value.error());
            return Result<bool>::failure(value.error());
        }
        name_->setText(value.value().name);
        locationType_->setCurrentIndex(static_cast<int>(value.value().type));
        const bool companyOrFactory = value.value().type == LocationType::Company ||
            value.value().type == LocationType::Factory;
        inventory_->setEnabled(!companyOrFactory || !value.value().countsAsInventory);
        inventory_->setChecked(value.value().countsAsInventory);
    }
    updateContentSize();
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
    } else if (entity_ == Entity::ProductModel) {
        const auto packaging = packagingSpec();
        if (!packaging.ok()) {
            showError(packaging.error());
            return false;
        }
        const auto value = id_
            ? service_.updateProductModel(id_, category_->currentData().toLongLong(),
                                          code_->text(), details_->text(), packaging.value())
            : service_.createProductModel(category_->currentData().toLongLong(),
                                          code_->text(), details_->text(), packaging.value());
        if (!value.ok())
            result = Result<bool>::failure(value.error());
    } else if (entity_ == Entity::Personnel) {
        const auto value = id_ ? service_.updatePersonnel(id_, name_->text())
                              : service_.createPersonnel(name_->text());
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
    if (field == QStringLiteral("typeCode") || field == QStringLiteral("modelCode") ||
        field == QStringLiteral("code"))
        return code_;
    if (field == QStringLiteral("categoryId") || field == QStringLiteral("productTypeId"))
        return category_;
    if (field == QStringLiteral("locationType"))
        return locationType_;
    if (field == QStringLiteral("countsAsInventory"))
        return inventory_;
    if (field == QStringLiteral("unitsPerBox"))
        return unitsPerBox_;
    if (field == QStringLiteral("boxesPerCarton"))
        return boxesPerCarton_;
    return nullptr;
}

void MasterDataDialog::showError(const ServiceError &error)
{
    error_->setText(error.message);
    if (auto *control = controlForField(error.field))
        control->setFocus();
}
