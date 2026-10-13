#pragma once

#include <QString>
#include <utility>
#include <variant>

struct ServiceError {
    QString code;
    QString message;
    QString field;
};

template<class T>
class Result {
public:
    static Result success(T value) { return Result(std::move(value)); }
    static Result failure(ServiceError error) { return Result(std::move(error)); }

    bool ok() const { return std::holds_alternative<T>(data_); }
    const T &value() const { return std::get<T>(data_); }
    T &value() { return std::get<T>(data_); }
    const ServiceError &error() const { return std::get<ServiceError>(data_); }

private:
    explicit Result(T value) : data_(std::move(value)) {}
    explicit Result(ServiceError error) : data_(std::move(error)) {}

    std::variant<T, ServiceError> data_;
};
