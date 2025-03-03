#include "processor/plan_mapper.h"
#include "planner/operator/scan/logical_rel_property_scan.h"
#include "processor/operator/scan/rel_property_scan.h"

using namespace kuzu::common;
using namespace kuzu::planner;

namespace kuzu {
namespace processor {

std::unique_ptr<PhysicalOperator> PlanMapper::mapRelPropertyScan(LogicalOperator* logicalOperator) {
    auto logicalRelPropertyScan = ku_dynamic_cast<LogicalRelPropertyScan*>(logicalOperator);
    
    // Map the child operator first (which should be an extension, scan, or other operator that produces relationships)
    auto childOperator = mapOperator(logicalOperator->getChild(0).get());
    
    // Create the RelPropertyScan as a unary operator with the mapped child
    auto physicalRelPropertyScan = std::make_unique<RelPropertyScan>(std::move(childOperator), getOperatorID());
    
    // Additional mapping logic can be added here if needed
    
    return physicalRelPropertyScan;
}

} // namespace processor
} // namespace kuzu 