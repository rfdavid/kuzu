#pragma once

#include "processor/operator/physical_operator.h"
#include "storage/local_storage/local_storage.h"

namespace kuzu {
namespace processor {

class RelPropertyScan : public PhysicalOperator {
public:
    // Backward compatible constructor with default values for tableId and propertyColumnId
    explicit RelPropertyScan(std::unique_ptr<PhysicalOperator> child, uint32_t id) 
        : PhysicalOperator(PhysicalOperatorType::REL_PROPERTY_SCAN, std::move(child), id, 
                           std::make_unique<OPPrintInfo>()),
          resultSet(nullptr), relIDVector(nullptr), propertyVector(nullptr),
          tableId(1), propertyColumnId(0) {}

    // Full constructor with all parameters
    explicit RelPropertyScan(std::unique_ptr<PhysicalOperator> child, common::table_id_t tableId,
                            common::column_id_t propertyColumnId, uint32_t id) 
        : PhysicalOperator(PhysicalOperatorType::REL_PROPERTY_SCAN, std::move(child), id, 
                           std::make_unique<OPPrintInfo>()),
          resultSet(nullptr), relIDVector(nullptr), propertyVector(nullptr),
          tableId(tableId), propertyColumnId(propertyColumnId) {}
    
    bool getNextTuplesInternal(ExecutionContext* context) override;

    void initLocalStateInternal(ResultSet* resultSet, ExecutionContext* context) override;
    
    std::unique_ptr<PhysicalOperator> clone() override {
        auto result = std::make_unique<RelPropertyScan>(getChild(0)->clone(), 
                                                         tableId, propertyColumnId, id);
        return result;
    }

private:
    ResultSet* resultSet;
    common::ValueVector* relIDVector;
    common::ValueVector* propertyVector;
    common::table_id_t tableId;
    common::column_id_t propertyColumnId;
};

} // namespace processor
} // namespace kuzu 