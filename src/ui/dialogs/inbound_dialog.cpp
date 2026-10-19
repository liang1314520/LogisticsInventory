#include "inbound_dialog.h"

#include "services/inbound_service.h"
#include "services/master_data_service.h"
#include "ui/widgets/date_time_input.h"
#include "ui/widgets/layout_utils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QShowEvent>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

InboundDialog::InboundDialog(MasterDataService &masterData, InboundService &service,
                             QWidget *parent)
    : QDialog(parent), masterData_(masterData), service_(service)
{
    setObjectName(QStringLiteral("inboundDialog"));
    setModal(true);
    setSizeGripEnabled(true);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    batchNo_ = new QLineEdit(this);
    batchNo_->setObjectName(QStringLiteral("batchNo"));
    batchNo_->setReadOnly(true);
    batchNo_->setPlaceholderText(QStringLiteral("保存时自动生成"));
    manualNumber_ = new QCheckBox(QStringLiteral("手动编号"), this);
    manualNumber_->setObjectName(QStringLiteral("manualNumber"));
    auto *numberLayout = new QHBoxLayout;
    numberLayout->addWidget(batchNo_, 1);
    numberLayout->addWidget(manualNumber_);
    connect(manualNumber_, &QCheckBox::toggled, this, [this](bool manual) {
        batchNo_->setReadOnly(!manual);
        if (!manual)
            batchNo_->setText(originalNumber_);
    });
    productType_ = new QComboBox(this);
    productType_->setObjectName(QStringLiteral("productTypeId"));
    materialModel_ = new QLineEdit(this);
    materialModel_->setObjectName(QStringLiteral("materialModel"));
    materialModel_->setPlaceholderText(QStringLiteral("必填，例如 V40"));
    quantity_ = new QLineEdit(this);
    quantity_->setObjectName(QStringLiteral("quantity"));
    quantity_->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[1-9][0-9]{0,18}")), quantity_));
    location_ = new QComboBox(this);
    location_->setObjectName(QStringLiteral("initialLocationId"));
    businessAt_ = new DateTimeInput(this);
    businessAt_->setObjectName(QStringLiteral("businessAt"));
    receivedBy_ = new QComboBox(this);
    receivedBy_->setObjectName(QStringLiteral("receivedById"));
    receivedBy_->setToolTip(QStringLiteral("确认入库前必选；请先在基础资料的人员名单中录入姓名"));
    note_ = new QTextEdit(this);
    note_->setObjectName(QStringLiteral("note"));

    form->addRow(QStringLiteral("批次号"), numberLayout);
    form->addRow(QStringLiteral("产品名称"), productType_);
    form->addRow(QStringLiteral("物料型号（必填）"), materialModel_);
    form->addRow(QStringLiteral("数量"), quantity_);
    form->addRow(QStringLiteral("公司入库库位"), location_);
    form->addRow(QStringLiteral("业务时间"), businessAt_);
    form->addRow(QStringLiteral("入库签收人"), receivedBy_);
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
    for (auto *edit : {batchNo_, materialModel_})
        connect(edit, &QLineEdit::textEdited, this, &InboundDialog::updateContentSize);
    for (auto *combo : {productType_, location_, receivedBy_})
        connect(combo, &QComboBox::activated, this, &InboundDialog::updateContentSize);
}

void InboundDialog::updateContentSize()
{
    batchNo_->setToolTip(batchNo_->text());
    materialModel_->setToolTip(materialModel_->text());
    LayoutUtils::fitDialogToContents(this, QSize(800, 560));
}

void InboundDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    updateContentSize();
}

void InboundDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::FontChange)
        QTimer::singleShot(0, this, &InboundDialog::updateContentSize);
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
        if (item.type == LocationType::Company && item.countsAsInventory)
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
    receivedBy_->clear();
    if (!readOnly_) {
        const auto people = masterData_.enabledPersonnelResult();
        if (!people.ok()) {
            showError(people.error());
            return Result<bool>::failure(people.error());
        }
        receivedBy_->addItem(people.value().isEmpty()
            ? QStringLiteral("请先到基础资料 → 人员名单录入") : QStringLiteral("请选择"), EntityId(0));
        for (const auto &person : people.value())
            receivedBy_->addItem(person.name, person.id);
    }
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
        if (readOnly_) {
            receivedBy_->addItem(loaded.value().receivedByName.isEmpty()
                ? QStringLiteral("未填写") : loaded.value().receivedByName, value.receivedById);
        } else if (value.receivedById > 0 && receivedBy_->findData(value.receivedById) < 0) {
            receivedBy_->addItem(loaded.value().receivedByName + QStringLiteral("（已停用）"), value.receivedById);
        }
        const auto choices = loadChoices(value.productTypeId, value.initialLocationId);
        if (!choices.ok())
            return choices;
        setDraft(value);
    } else {
        const auto choices = loadChoices();
        if (!choices.ok())
            return choices;
    }

    originalNumber_ = id ? value.batchNo : QString();
    manualNumber_->setChecked(false);
    batchNo_->setText(originalNumber_);
    batchNo_->setReadOnly(true);
    manualNumber_->setVisible(!readOnly_);
    materialModel_->setPlaceholderText(readOnly_ ? QStringLiteral("未填写")
                                                : QStringLiteral("必填，例如 V40"));
    const QList<QWidget *> controls{batchNo_, manualNumber_, productType_, materialModel_,
                                    quantity_, location_, businessAt_, receivedBy_, note_};
    for (QWidget *widget : controls)
        widget->setEnabled(!readOnly_);
    save_->setVisible(!readOnly_);
    updateContentSize();
    return Result<bool>::success(true);
}

void InboundDialog::setDraft(const InboundDraft &value)
{
    manualNumber_->setChecked(!value.batchNo.trimmed().isEmpty());
    batchNo_->setText(value.batchNo);
    materialModel_->setText(value.materialModel);
    receivedBy_->setCurrentIndex(receivedBy_->findData(value.receivedById));
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
    updateContentSize();
}

InboundDraft InboundDialog::draft() const
{
    bool ok = false;
    const qint64 quantity = quantity_->text().toLongLong(&ok);
    return {batchNo_->text(), productType_->currentData().toLongLong(), ok ? quantity : 0,
            location_->currentData().toLongLong(), businessAt_->dateTime(), note_->toPlainText(),
            materialModel_->text(), receivedBy_->currentData().toLongLong()};
}

bool InboundDialog::save()
{
    if (manualNumber_->isChecked() && batchNo_->text().trimmed().isEmpty()) {
        showError({QStringLiteral("required"), QStringLiteral("手动批次号不能为空"),
                   QStringLiteral("batchNo")});
        return false;
    }
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
    if (field == QStringLiteral("materialModel"))
        return materialModel_;
    if (field == QStringLiteral("quantity"))
        return quantity_;
    if (field == QStringLiteral("initialLocationId"))
        return location_;
    if (field == QStringLiteral("businessAt"))
        return businessAt_;
    if (field == QStringLiteral("receivedById"))
        return receivedBy_;
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
