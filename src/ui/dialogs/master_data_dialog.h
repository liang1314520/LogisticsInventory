#pragma once

#include "core/models.h"
#include "core/result.h"

#include <QDialog>

class MasterDataService;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

class MasterDataDialog final : public QDialog {
    Q_OBJECT
public:
    enum class Entity { Category, ProductType, Location, ProductModel, Personnel };
    MasterDataDialog(MasterDataService &service, Entity entity, QWidget *parent = nullptr);
    Result<bool> initialize(EntityId id = 0);
    bool save();

protected:
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void updateContentSize();
    void showError(const ServiceError &error);
    QWidget *controlForField(const QString &field) const;

    MasterDataService &service_;
    Entity entity_;
    EntityId id_ = 0;
    QLineEdit *name_ = nullptr;
    QLineEdit *code_ = nullptr;
    QLineEdit *details_ = nullptr;
    QComboBox *category_ = nullptr;
    QComboBox *locationType_ = nullptr;
    QCheckBox *inventory_ = nullptr;
    QLabel *error_ = nullptr;
    QPushButton *save_ = nullptr;
};
