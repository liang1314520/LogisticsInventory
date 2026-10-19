#pragma once

#include <QDateTime>
#include <QWidget>

class QDateEdit;
class QPushButton;
class QTimeEdit;

class DateTimeInput final : public QWidget {
    Q_OBJECT
public:
    explicit DateTimeInput(QWidget *parent = nullptr);
    QDateTime dateTime() const;
    void setDateTime(const QDateTime &value);
    void setNowButtonVisible(bool visible);

signals:
    void dateTimeChanged(const QDateTime &value);

private:
    QDateEdit *date_ = nullptr;
    QTimeEdit *time_ = nullptr;
    QPushButton *now_ = nullptr;
    QDateTime value_;
};
