#pragma once

#include "processor/operator/physical_operator.h"
#include "processor/execution_context.h"

namespace kuzu {
namespace processor {

class RelPropertyScan : public PhysicalOperator {
public:
    // Constructor for unary operator (with one child)
    explicit RelPropertyScan(std::unique_ptr<PhysicalOperator> child, uint32_t id) 
        : PhysicalOperator(PhysicalOperatorType::REL_PROPERTY_SCAN, std::move(child), id, 
                           std::make_unique<OPPrintInfo>()) {}

    bool getNextTuplesInternal(ExecutionContext* context) override {
        return false;
    }

    std::unique_ptr<PhysicalOperator> clone() override {
        auto result = std::make_unique<RelPropertyScan>(getChild(0)->clone(), id);
        return result;
    }
};

} // namespace processor
} // namespace kuzu 