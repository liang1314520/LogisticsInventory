#pragma once

#include "core/models.h"
#include "core/result.h"

#include <functional>
#include <QWidget>

class InventoryQueryService;
class QLabel;
class LogisticsService;
class MasterDataService;
class QPushButton;
class QTableWidget;

class LogisticsPage final : public QWidget {
    Q_OBJECT
public:
    LogisticsPage(MasterDataService &masterData, InventoryQueryService &queries,
                  LogisticsService &service, DocumentType type, QWidget *parent = nullptr);
    Result<bool> refresh();
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

private slots:
    void updateActions();
    void editSelected();
    void viewSelected();
    void deleteSelected();
    void confirmSelected();
    void cancelSelected();
    void copySelected();

private:
    EntityId selectedId() const;
    bool confirmAction(const QString &text) const;
    void setError(const ServiceError &error = {});
    MasterDataService &masterData_;
    InventoryQueryService &queries_;
    LogisticsService &service_;
    DocumentType type_;
    QTableWidget *table_ = nullptr;
    QLabel *error_ = nullptr;
    QPushButton *edit_ = nullptr;
    QPushButton *view_ = nullptr;
    QPushButton *remove_ = nullptr;
    QPushButton *confirm_ = nullptr;
    QPushButton *cancel_ = nullptr;
    QPushButton *copy_ = nullptr;
    std::function<bool(const QString &)> confirmation_;
    ServiceError lastError_;
};
