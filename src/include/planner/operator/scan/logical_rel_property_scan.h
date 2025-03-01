#pragma once

#include "binder/expression/expression_util.h"
#include "planner/operator/logical_operator.h"
#include "storage/predicate/column_predicate.h"


namespace kuzu {
namespace planner {

class LogicalRelPropertyScan final : public LogicalOperator {
public:
    explicit LogicalRelPropertyScan() : LogicalOperator{LogicalOperatorType::REL_PROPERTY_SCAN} {}

    void computeFactorizedSchema() override;
    void computeFlatSchema() override;

    inline std::string getExpressionsForPrinting() const override { return std::string(); }

    inline std::unique_ptr<LogicalOperator> copy() override {
        return std::make_unique<LogicalRelPropertyScan>();
    }
};

} // namespace planner
} // namespace kuzu
