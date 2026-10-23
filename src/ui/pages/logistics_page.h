#pragma once

#include "core/models.h"
#include "core/result.h"
#include "services/batch_workflow_service.h"

#include <functional>
#include <QWidget>

class InventoryQueryService;
class QComboBox;
class QLabel;
class LogisticsService;
class MasterDataService;
class QPushButton;
class QTableWidget;

class LogisticsPage final : public QWidget {
    Q_OBJECT
public:
    LogisticsPage(MasterDataService &masterData, InventoryQueryService &queries,
                  LogisticsService &service, DocumentType type, QWidget *parent = nullptr,
                  BatchWorkflowService *workflow = nullptr);
    Result<bool> refresh();
    void setCurrentBatch(EntityId batchId);
    EntityId currentBatchId() const;
    Result<bool> selectBatch(EntityId batchId);
    void setWorkflowMessage(const QString &message);
    ServiceError lastError() const;
    void setConfirmationCallback(std::function<bool(const QString &)> callback);
    Result<bool> openCreateDialog();
    Result<bool> openEditDialog(EntityId id);
    Result<bool> openViewDialog(EntityId id);

signals:
    void createRequested(DocumentType type);
    void editRequested(EntityId id);
    void viewRequested(EntityId id);
    void dataChanged();
    void batchSelected(EntityId batchId);

private slots:
    void updateActions();
    void editSelected();
    void viewSelected();
    void cancelSelected();

private:
    void applyFilters();
    EntityId selectedId() const;
    WorkflowAction actionForType() const;
    bool confirmAction(const QString &text) const;
    void setError(const ServiceError &error = {});
    MasterDataService &masterData_;
    InventoryQueryService &queries_;
    LogisticsService &service_;
    DocumentType type_;
    QTableWidget *table_ = nullptr;
    QComboBox *sourceFilter_ = nullptr;
    int sortColumn_ = -1;
    Qt::SortOrder sortOrder_ = Qt::AscendingOrder;
    QLabel *error_ = nullptr;
    QLabel *workflowContext_ = nullptr;
    BatchWorkflowService *workflow_ = nullptr;
    EntityId currentBatchId_ = 0;
    QPushButton *edit_ = nullptr;
    QPushButton *view_ = nullptr;
    QPushButton *cancel_ = nullptr;
    std::function<bool(const QString &)> confirmation_;
    ServiceError lastError_;
};
