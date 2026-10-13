#pragma once

#include "core/result.h"
#include "repositories/document_repository.h"

#include <QDialog>

class InboundService;
class MasterDataService;
class QComboBox;
class QDateTimeEdit;
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
    void setDraft(const InboundDraft &draft);
    InboundDraft draft() const;
    bool save();
    bool isReadOnly() const;

private:
    Result<bool> loadChoices(EntityId currentProductType = 0, EntityId currentLocation = 0);
    void showError(const ServiceError &error);
    QWidget *controlForField(const QString &field) const;

    MasterDataService &masterData_;
    InboundService &service_;
    EntityId id_ = 0;
    bool readOnly_ = false;
    QLineEdit *batchNo_ = nullptr;
    QComboBox *productType_ = nullptr;
    QLineEdit *quantity_ = nullptr;
    QComboBox *location_ = nullptr;
    QDateTimeEdit *businessAt_ = nullptr;
    QTextEdit *note_ = nullptr;
    QLabel *error_ = nullptr;
    QPushButton *save_ = nullptr;
};
