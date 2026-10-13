#include "inbound_dialog.h"

#include "services/inbound_service.h"
#include "services/master_data_service.h"

#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QList>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QTextEdit>
#include <QVBoxLayout>

InboundDialog::InboundDialog(MasterDataService &masterData, InboundService &service,
                             QWidget *parent)
    : QDialog(parent), masterData_(masterData), service_(service)
{
    setObjectName(QStringLiteral("inboundDialog"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    batchNo_ = new QLineEdit(this);
    batchNo_->setObjectName(QStringLiteral("batchNo"));
    productType_ = new QComboBox(this);
    productType_->setObjectName(QStringLiteral("productTypeId"));
    quantity_ = new QLineEdit(this);
    quantity_->setObjectName(QStringLiteral("quantity"));
    quantity_->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[1-9][0-9]{0,18}")), quantity_));
    location_ = new QComboBox(this);
    location_->setObjectName(QStringLiteral("initialLocationId"));
    businessAt_ = new QDateTimeEdit(QDateTime::currentDateTime(), this);
    businessAt_->setObjectName(QStringLiteral("businessAt"));
    businessAt_->setCalendarPopup(true);
    note_ = new QTextEdit(this);
    note_->setObjectName(QStringLiteral("note"));

    form->addRow(QStringLiteral("批次号"), batchNo_);
    form->addRow(QStringLiteral("产品类型"), productType_);
    form->addRow(QStringLiteral("数量"), quantity_);
    form->addRow(QStringLiteral("初始库位"), location_);
    form->addRow(QStringLiteral("业务时间"), businessAt_);
    form->addRow(QStringLiteral("备注"), note_);
    layout->addLayout(form);

    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("errorLabel"));
    error_->setStyleSheet(QStringLiteral("color:#b00020"));
    error_->setWordWrap(true);
    layout->addWidget(error_);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    save_ = buttons->button(QDialogButtonBox::Save);
    save_->setObjectName(QStringLiteral("saveButton"));
    connect(save_, &QPushButton::clicked, this, [this] {
        if (save()) accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

Result<bool> InboundDialog::loadChoices(EntityId currentProductType, EntityId currentLocation)
{
    const auto productTypes = masterData_.enabledProductTypesResult();
    if (!productTypes.ok()) {
        showError(productTypes.error());
        return Result<bool>::failure(productTypes.error());
    }
    const auto locations = masterData_.enabledLocationsResult();
    if (!locations.ok()) {
        showError(locations.error());
        return Result<bool>::failure(locations.error());
    }

    productType_->clear();
    for (const auto &item : productTypes.value()) {
        const QString text = item.description.isEmpty()
            ? item.typeCode
            : item.typeCode + QStringLiteral(" / ") + item.description;
        productType_->addItem(text, item.id);
    }
    if (currentProductType > 0 && productType_->findData(currentProductType) < 0) {
        const auto item = masterData_.productTypeById(currentProductType);
        if (!item.ok()) {
            showError(item.error());
            return Result<bool>::failure(item.error());
        }
        const QString text = item.value().typeCode + QStringLiteral(" (已停用/历史)");
        productType_->addItem(text, item.value().id);
    }

    location_->clear();
    for (const auto &item : locations.value()) {
        if (item.countsAsInventory)
            location_->addItem(item.name, item.id);
    }
    if (currentLocation > 0 && location_->findData(currentLocation) < 0) {
        const auto item = masterData_.locationById(currentLocation);
        if (!item.ok()) {
            showError(item.error());
            return Result<bool>::failure(item.error());
        }
        location_->addItem(item.value().name + QStringLiteral(" (已停用/历史)"), item.value().id);
    }
    return Result<bool>::success(true);
}

Result<bool> InboundDialog::initialize(EntityId id, bool readOnly)
{
    id_ = id;
    readOnly_ = readOnly;
    InboundDraft value;
    if (id) {
        const auto loaded = service_.byId(id);
        if (!loaded.ok()) {
            showError(loaded.error());
            return Result<bool>::failure(loaded.error());
        }
        if (!readOnly && loaded.value().status != DocumentStatus::Draft) {
            const ServiceError status{QStringLiteral("invalid_status"),
                                      QStringLiteral("仅草稿可编辑"), QStringLiteral("status")};
            showError(status);
            return Result<bool>::failure(status);
        }
        value = loaded.value();
        const auto choices = loadChoices(value.productTypeId, value.initialLocationId);
        if (!choices.ok())
            return choices;
        setDraft(value);
    } else {
        const auto choices = loadChoices();
        if (!choices.ok())
            return choices;
    }

    const QList<QWidget *> controls{batchNo_, productType_, quantity_, location_, businessAt_, note_};
    for (QWidget *widget : controls)
        widget->setEnabled(!readOnly_);
    save_->setVisible(!readOnly_);
    return Result<bool>::success(true);
}

void InboundDialog::setDraft(const InboundDraft &value)
{
    batchNo_->setText(value.batchNo);
    quantity_->setText(QString::number(value.quantity));
    const int productTypeIndex = productType_->findData(value.productTypeId);
    if (productTypeIndex >= 0)
        productType_->setCurrentIndex(productTypeIndex);
    const int locationIndex = location_->findData(value.initialLocationId);
    if (locationIndex >= 0)
        location_->setCurrentIndex(locationIndex);
    businessAt_->setDateTime(value.businessAt.isValid() ? value.businessAt
                                                         : QDateTime::currentDateTime());
    note_->setPlainText(value.note);
}

InboundDraft InboundDialog::draft() const
{
    bool ok = false;
    const qint64 quantity = quantity_->text().toLongLong(&ok);
    return {batchNo_->text(), productType_->currentData().toLongLong(), ok ? quantity : 0,
            location_->currentData().toLongLong(), businessAt_->dateTime(), note_->toPlainText()};
}

bool InboundDialog::save()
{
    const auto result = id_
        ? service_.updateDraft(id_, draft())
        : [&] {
              const auto created = service_.createDraft(draft());
              return created.ok() ? Result<bool>::success(true)
                                  : Result<bool>::failure(created.error());
          }();
    if (!result.ok()) {
        showError(result.error());
        return false;
    }
    error_->clear();
    return true;
}

QWidget *InboundDialog::controlForField(const QString &field) const
{
    if (field == QStringLiteral("batchNo"))
        return batchNo_;
    if (field == QStringLiteral("productTypeId"))
        return productType_;
    if (field == QStringLiteral("quantity"))
        return quantity_;
    if (field == QStringLiteral("initialLocationId"))
        return location_;
    if (field == QStringLiteral("businessAt"))
        return businessAt_;
    return nullptr;
}

void InboundDialog::showError(const ServiceError &value)
{
    error_->setText(value.message);
    if (auto *control = controlForField(value.field))
        control->setFocus();
}

bool InboundDialog::isReadOnly() const
{
    return readOnly_;
}
