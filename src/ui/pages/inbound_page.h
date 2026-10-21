#pragma once

#include "core/models.h"
#include "core/result.h"

#include <functional>
#include <QWidget>

class InboundService;
class QLabel;
class MasterDataService;
class QPushButton;
class QTableWidget;

class InboundPage final : public QWidget {
    Q_OBJECT
public:
    explicit InboundPage(MasterDataService &masterData, InboundService &service, QWidget *parent = nullptr);
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
    void createRequested();
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
    EntityId selectedId() const;
    bool confirmAction(const QString &text) const;
    void setError(const ServiceError &error = {});
    MasterDataService &masterData_;
    InboundService &service_;
    QTableWidget *table_ = nullptr;
    QLabel *error_ = nullptr;
    QLabel *workflowContext_ = nullptr;
    EntityId currentBatchId_ = 0;
    QPushButton *edit_ = nullptr;
    QPushButton *view_ = nullptr;
    QPushButton *cancel_ = nullptr;
    std::function<bool(const QString &)> confirmation_;
    ServiceError lastError_;
};
