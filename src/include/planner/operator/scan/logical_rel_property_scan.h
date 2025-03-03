#pragma once

#include "binder/expression/expression_util.h"
#include "planner/operator/logical_operator.h"
#include "storage/predicate/column_predicate.h"

namespace kuzu {
namespace planner {

class LogicalRelPropertyScan final : public LogicalOperator {
public:
    explicit LogicalRelPropertyScan(std::shared_ptr<LogicalOperator> child)
        : LogicalOperator{LogicalOperatorType::REL_PROPERTY_SCAN, std::move(child)} {}

    void computeFactorizedSchema() override;
    void computeFlatSchema() override;

    inline std::string getExpressionsForPrinting() const override { return std::string(); }

    inline std::unique_ptr<LogicalOperator> copy() override {
        auto result = std::make_unique<LogicalRelPropertyScan>(getChild(0)->copy());
        return result;
    }

private:
    // binder::expression_vector properties_;
};

} // namespace planner
} // namespace kuzu
