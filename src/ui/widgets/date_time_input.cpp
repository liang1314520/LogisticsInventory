#include "date_time_input.h"

#include <QDateEdit>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimeEdit>

DateTimeInput::DateTimeInput(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    date_ = new QDateEdit(this);
    date_->setObjectName(QStringLiteral("date"));
    date_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    date_->setCalendarPopup(true);
    time_ = new QTimeEdit(this);
    time_->setObjectName(QStringLiteral("time"));
    time_->setDisplayFormat(QStringLiteral("HH:mm"));
    time_->setToolTip(QStringLiteral("选择小时和分钟，可直接输入或使用上下按钮调整"));
    now_ = new QPushButton(QStringLiteral("现在"), this);
    now_->setObjectName(QStringLiteral("nowButton"));
    layout->addWidget(date_, 1);
    layout->addWidget(time_);
    layout->addWidget(now_);
    setFocusProxy(date_);
    setDateTime(QDateTime::currentDateTime());
    connect(date_, &QDateEdit::dateChanged, this, [this](const QDate &date) {
        auto value = value_;
        value.setDate(date);
        setDateTime(value);
    });
    connect(time_, &QTimeEdit::timeChanged, this, [this](const QTime &time) {
        auto value = value_;
        value.setTime(QTime(time.hour(), time.minute()));
        setDateTime(value);
    });
    connect(now_, &QPushButton::clicked, this, [this] {
        setDateTime(QDateTime::currentDateTime());
    });
}

QDateTime DateTimeInput::dateTime() const
{
    return value_;
}

void DateTimeInput::setDateTime(const QDateTime &value)
{
    const bool changed = value_ != value;
    value_ = value;
    const QSignalBlocker dateBlocker(date_);
    const QSignalBlocker timeBlocker(time_);
    date_->setDate(value.date());
    time_->setTime(value.time());
    if (changed)
        emit dateTimeChanged(value_);
}

void DateTimeInput::setNowButtonVisible(bool visible)
{
    now_->setVisible(visible);
}
