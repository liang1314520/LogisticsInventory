#pragma once

#include "core/result.h"
#include "repositories/document_repository.h"

#include <QDialog>

class InboundService;
class MasterDataService;
class QCheckBox;
class QComboBox;
class DateTimeInput;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextEdit;

class InboundDialog final : public QDialog {
    Q_OBJECT
public:
    InboundDialog(MasterDataService &masterData, InboundService &service,
                  QWidget *parent = nullptr);
    Result<bool> initialize(EntityId id = 0, bool readOnly = false);
    void setInput(const InboundInput &input);
    InboundInput input() const;
    bool save();
    bool isReadOnly() const;

protected:
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void updateContentSize();
    Result<bool> loadChoices(EntityId currentProductType = 0, EntityId currentLocation = 0);
    void showError(const ServiceError &error);
    QWidget *controlForField(const QString &field) const;

    MasterDataService &masterData_;
    InboundService &service_;
    EntityId id_ = 0;
    bool readOnly_ = false;
    QString originalNumber_;
    QLineEdit *batchNo_ = nullptr;
    QCheckBox *manualNumber_ = nullptr;
    QComboBox *productType_ = nullptr;
    QLineEdit *materialModel_ = nullptr;
    QLineEdit *quantity_ = nullptr;
    QComboBox *location_ = nullptr;
    DateTimeInput *businessAt_ = nullptr;
    QComboBox *receivedBy_ = nullptr;
    QTextEdit *note_ = nullptr;
    QLabel *saveHint_ = nullptr;
    QLabel *error_ = nullptr;
    QPushButton *save_ = nullptr;
};
