#pragma once

#include "core/models.h"
#include "core/result.h"

Result<bool> validatePackagingSpec(const PackagingSpec &spec);
Result<qint64> calculatePackagingQuantity(const PackagingQuantities &quantities,
                                          const PackagingSpec &spec);
