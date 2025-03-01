#include "binder/expression/property_expression.h"
#include "planner/operator/scan/logical_rel_property_scan.h"
#include "planner/planner.h"

using namespace kuzu::common;
using namespace kuzu::binder;

namespace kuzu {
namespace planner {

void Planner::appendRelPropertyScan(std::shared_ptr<NodeExpression> boundNode,
    std::shared_ptr<NodeExpression> nbrNode, std::shared_ptr<RelExpression> rel,
    const binder::expression_vector& properties, LogicalPlan& plan) {
    auto relScan = std::make_shared<LogicalRelPropertyScan>();
    plan.setLastOperator(std::move(relScan));
}

} // namespace planner
} // namespace kuzu
