#pragma once

#include "core/result.h"
#include "services/batch_workflow_service.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

class WorkflowBar final : public QWidget {
    Q_OBJECT
public:
    explicit WorkflowBar(BatchWorkflowService &service, QWidget *parent = nullptr);

    Result<bool> refresh();
    Result<bool> setBatch(EntityId batchId);
    EntityId currentBatchId() const;
    void setExpanded(bool expanded);
    bool isExpanded() const;
    int expandedHeightForWidth(int width) const;

signals:
    void batchChanged(EntityId batchId);
    void nextRequested(int pageIndex, EntityId batchId);
    void trackingRequested(EntityId batchId);
    void expandedChanged(bool expanded);

private slots:
    void requestNext();
    void requestTracking();

private:
    Result<bool> loadCurrent();
    void clearState();
    void showError(const ServiceError &error);

    BatchWorkflowService &service_;
    QComboBox *selector_ = nullptr;
    QLabel *status_ = nullptr;
    QTableWidget *steps_ = nullptr;
    QLabel *message_ = nullptr;
    QPushButton *next_ = nullptr;
    QPushButton *tracking_ = nullptr;
    QPushButton *expand_ = nullptr;
    EntityId batchId_ = 0;
    BatchWorkflowState state_;
};
