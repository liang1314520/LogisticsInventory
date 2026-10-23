#pragma once

#include "core/result.h"
#include "repositories/document_repository.h"

#include <QDialog>

class InventoryQueryService;
class LogisticsService;
class MasterDataService;
class QCheckBox;
class QComboBox;
class DateTimeInput;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTextEdit;

class LogisticsDialog final : public QDialog {
    Q_OBJECT
public:
    LogisticsDialog(MasterDataService &masterData, InventoryQueryService &queries,
                    LogisticsService &service, DocumentType type, QWidget *parent = nullptr);
    Result<bool> initialize(EntityId id = 0, bool readOnly = false,
                            EntityId preferredBatchId = 0);
    Result<bool> setSourceLocation(EntityId id);
    QVector<EntityId> visibleBatchIds() const;
    Result<bool> addLine();
    Result<bool> setLineBatch(int row, EntityId batchId);
    Result<bool> setInput(const LogisticsInput &input);
    LogisticsInput input() const;
    bool save();
    bool isReadOnly() const;

protected:
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void updateContentSize();
    struct BatchChoice {
        EntityId id = 0;
        EntityId productTypeId = 0;
        QString text;
    };

    Result<bool> loadLocations(EntityId currentSource = 0, EntityId currentDestination = 0);
    Result<bool> loadBatches(EntityId locationId);
    Result<bool> populateModels(int row, EntityId modelId = -1,
                                const LogisticsLine *persistedLine = nullptr);
    Result<bool> populateLine(int row, const LogisticsLineInput &line,
                              const LogisticsLine *persistedLine = nullptr);
    Result<bool> refreshDestinations(EntityId currentDestination = 0,
                                     bool preserveHistorical = false);
    void showError(const ServiceError &error);
    QWidget *controlForField(const QString &field) const;

    MasterDataService &masterData_;
    InventoryQueryService &queries_;
    LogisticsService &service_;
    DocumentType type_;
    EntityId id_ = 0;
    EntityId preferredBatchId_ = 0;
    bool readOnly_ = false;
    QString originalNumber_;
    QVector<BatchChoice> batches_;
    LogisticsDocument original_;
    QLineEdit *documentNo_ = nullptr;
    QCheckBox *manualNumber_ = nullptr;
    QComboBox *source_ = nullptr;
    QComboBox *destination_ = nullptr;
    DateTimeInput *businessAt_ = nullptr;
    QComboBox *outboundConfirmedBy_ = nullptr;
    QTextEdit *note_ = nullptr;
    QTableWidget *lines_ = nullptr;
    QLabel *saveHint_ = nullptr;
    QLabel *error_ = nullptr;
    QPushButton *add_ = nullptr;
    QPushButton *remove_ = nullptr;
    QPushButton *save_ = nullptr;
};
