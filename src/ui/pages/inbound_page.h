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
    InboundService &service_;
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
