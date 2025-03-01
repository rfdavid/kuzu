#include "planner/operator/scan/logical_rel_property_scan.h"
#include "binder/expression/literal_expression.h"
#include "common/constants.h"

using namespace kuzu::common;

namespace kuzu {
namespace planner {

void LogicalRelPropertyScan::computeFactorizedSchema() {
    copyChildSchema(0);
//    createEmptySchema();
//    schema->createGroup();
}

void LogicalRelPropertyScan::computeFlatSchema() {
    copyChildSchema(0);
//    createEmptySchema();
//    schema->createGroup();
}

} // namespace planner
} // namespace kuzu
