#include "packaging.h"

#include <limits>

Result<bool> validatePackagingSpec(const PackagingSpec &spec)
{
    const auto fail = [](const QString &message, const QString &field) {
        return Result<bool>::failure({QStringLiteral("invalid_packaging"), message, field});
    };
    if (spec.unitsPerBox < 0)
        return fail(QStringLiteral("每盒个数不能为负数"), QStringLiteral("unitsPerBox"));
    if (spec.boxesPerCarton < 0)
        return fail(QStringLiteral("每箱盒数不能为负数"), QStringLiteral("boxesPerCarton"));
    if (spec.unitsPerBox == 0 && spec.boxesPerCarton == 0)
        return Result<bool>::success(true);
    if (spec.unitsPerBox == 0)
        return fail(QStringLiteral("设置包装时，每盒个数必须为正整数"), QStringLiteral("unitsPerBox"));
    if (spec.boxesPerCarton == 0)
        return fail(QStringLiteral("设置包装时，每箱盒数必须为正整数"), QStringLiteral("boxesPerCarton"));
    if (spec.unitsPerBox > std::numeric_limits<qint64>::max() / spec.boxesPerCarton)
        return fail(QStringLiteral("每箱总个数超出范围"), QStringLiteral("boxesPerCarton"));
    return Result<bool>::success(true);
}

Result<qint64> calculatePackagingQuantity(const PackagingQuantities &quantities,
                                          const PackagingSpec &spec)
{
    const auto fail = [](const QString &message, const QString &field) {
        return Result<qint64>::failure({QStringLiteral("invalid_quantity"), message, field});
    };
    if (quantities.cartonCount < 0)
        return fail(QStringLiteral("箱数必须为非负整数"), QStringLiteral("cartonCount"));
    if (quantities.boxCount < 0)
        return fail(QStringLiteral("盒数必须为非负整数"), QStringLiteral("boxCount"));
    if (quantities.looseQuantity < 0)
        return fail(QStringLiteral("散个数必须为非负整数"), QStringLiteral("looseQuantity"));
    const auto valid = validatePackagingSpec(spec);
    if (!valid.ok())
        return Result<qint64>::failure(valid.error());
    if (spec.unitsPerBox == 0) {
        if (quantities.cartonCount > 0 || quantities.boxCount > 0)
            return fail(QStringLiteral("该型号未设置包装规格，只能按个填写"),
                        quantities.cartonCount > 0 ? QStringLiteral("cartonCount")
                                                   : QStringLiteral("boxCount"));
        return Result<qint64>::success(quantities.looseQuantity);
    }

    const qint64 maximum = std::numeric_limits<qint64>::max();
    const qint64 unitsPerCarton = spec.unitsPerBox * spec.boxesPerCarton;
    qint64 total = quantities.looseQuantity;
    const auto append = [maximum, &total](qint64 count, qint64 factor) {
        if (count > (maximum - total) / factor)
            return false;
        total += count * factor;
        return true;
    };
    if (!append(quantities.cartonCount, unitsPerCarton))
        return fail(QStringLiteral("箱数换算后的总个数超出范围"), QStringLiteral("cartonCount"));
    if (!append(quantities.boxCount, spec.unitsPerBox))
        return fail(QStringLiteral("盒数换算后的总个数超出范围"), QStringLiteral("boxCount"));
    return Result<qint64>::success(total);
}
