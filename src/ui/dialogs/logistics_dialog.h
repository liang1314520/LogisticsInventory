#pragma once

#include "core/result.h"
#include "repositories/document_repository.h"

#include <QDialog>

class InventoryQueryService;
class LogisticsService;
class MasterDataService;
class QComboBox;
class QDateTimeEdit;
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
    Result<bool> initialize(EntityId id = 0, bool readOnly = false);
    Result<bool> setSourceLocation(EntityId id);
    QVector<EntityId> visibleBatchIds() const;
    Result<bool> addLine();
    Result<bool> setLineBatch(int row, EntityId batchId);
    Result<bool> setDraft(const LogisticsDraft &draft);
    LogisticsDraft draft() const;
    bool save();
    bool isReadOnly() const;

private:
    struct BatchChoice {
        EntityId id = 0;
        EntityId productTypeId = 0;
        QString text;
    };

    Result<bool> loadLocations(EntityId currentSource = 0, EntityId currentDestination = 0);
    Result<bool> populateLine(int row, const LogisticsLineDraft &line,
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
    bool readOnly_ = false;
    QVector<BatchChoice> batches_;
    QLineEdit *documentNo_ = nullptr;
    QComboBox *source_ = nullptr;
    QComboBox *destination_ = nullptr;
    QDateTimeEdit *businessAt_ = nullptr;
    QTextEdit *note_ = nullptr;
    QTableWidget *lines_ = nullptr;
    QLabel *error_ = nullptr;
    QPushButton *add_ = nullptr;
    QPushButton *remove_ = nullptr;
    QPushButton *save_ = nullptr;
};
