#include "processor/operator/scan/rel_property_scan.h"
#include "processor/execution_context.h"
#include "storage/store/rel_table.h"
#include "storage/storage_manager.h"
#include "storage/storage_utils.h"

#include <iostream>


namespace kuzu {
namespace processor {

void RelPropertyScan::initLocalStateInternal(ResultSet* resultSet, ExecutionContext* context) {
    this->resultSet = resultSet;
    
    // relIDVector contain the rel IDs to be scanned
    if (resultSet->dataChunks.size() > 0 && resultSet->getDataChunk(1)->getNumValueVectors() > 0) {
        relIDVector = resultSet->getDataChunk(1)->valueVectors[2].get();
    }

    auto propertyDataChunk = resultSet->getDataChunk(2); 
    if (propertyDataChunk->valueVectors.size() == 0) {
        propertyDataChunk->valueVectors.resize(1);
    }
//    propertyDataChunk->insert(0, std::make_shared<common::ValueVector>(common::LogicalType{common::LogicalTypeID::STRING}, 
 //           context->clientContext->getMemoryManager()));
//    propertyVector = resultSet->getDataChunk(2)->valueVectors[0].get();
    propertyVector = resultSet->getDataChunk(1)->valueVectors[1].get();
}

bool RelPropertyScan::getNextTuplesInternal(ExecutionContext* context) {
    if (!getChild(0)->getNextTuple(context)) {
        return false;
    }
    
    auto storageManager = context->clientContext->getStorageManager();
    auto transaction = context->clientContext->getTransaction();
    
    auto relTable = common::ku_dynamic_cast<storage::RelTable*>(
        storageManager->getTable(tableId));
    
    if (!relTable || !relTable->propertyNodeGroups) {
        return true;
    }
    
    // Get relation IDs from the relIDVector
    const auto& selVector = relIDVector->state->getSelVector();
    auto numRows = selVector.getSelSize();
    
    if (numRows == 0) {
        return true;
    }
    
    // Reset the property vector's selection vector to match the input
    propertyVector->state->getSelVectorUnsafe().setToFiltered(numRows);

    // For each relation ID, look up the property in propertyNodeGroups
    auto nodeGroups = relTable->propertyNodeGroups.get();

    
    // Process each relation ID and scan the property
    for (common::sel_t i = 0; i < numRows; i++) {
        auto pos = selVector[i];
        auto relID = relIDVector->getValue<common::internalID_t>(pos);
        
        // Get node group index and offset within the group
        auto nodeGroupIdx = storage::StorageUtils::getNodeGroupIdx(relID.offset);
        auto offsetInNodeGroup = relID.offset - storage::StorageUtils::getStartOffsetOfNodeGroup(nodeGroupIdx);

        // Set to NULL by default
        propertyVector->setNull(i, true);
        
        if (nodeGroupIdx < nodeGroups->getNumNodeGroups()) {
            auto nodeGroup = nodeGroups->getNodeGroup(nodeGroupIdx);
            if (nodeGroup && offsetInNodeGroup < nodeGroup->getNumRows()) {

                try {
                    auto tmpVector = std::make_shared<common::ValueVector>(
                        common::LogicalType{common::LogicalTypeID::STRING},
                        context->clientContext->getMemoryManager());
                    
                    if (!tmpVector->state) {
                        tmpVector->setState(std::make_shared<common::DataChunkState>());
                    }

                    // set up a scan state for this specific row
                    storage::TableScanState scanState(relTable->getTableID(), {propertyColumnId});
                    scanState.outputVectors.push_back(tmpVector.get());
                    scanState.nodeGroupScanState = std::make_unique<storage::NodeGroupScanState>(1);
                    scanState.nodeGroup = nodeGroup;
                    scanState.source = storage::TableScanSource::COMMITTED;
                    
                    // initialize outState to avoid null pointer in scan operation
                    scanState.outState = tmpVector->state.get();

                    // scan only the single value we need
                    storage::NodeGroupScanResult result =
                        nodeGroup->scan(transaction, scanState, offsetInNodeGroup, 1);

                    std::cout << "Scan result numRows: " << result.numRows << std::endl;

                    auto value = tmpVector->getAsValue(0);
                    if (result.numRows > 0) {
                        if (!tmpVector->isNull(0)) {
                            std::cout << "Scan retrieved value: " << value->toString() << std::endl;
                        } else {
                            std::cout << "Scan result is NULL" << std::endl;
                        }
                    }

                    if (result.numRows > 0 && !tmpVector->isNull(0)) {
                        propertyVector->setValue(i, value->toString());
                        propertyVector->setNull(i, false);
                    }

                } catch (...) {
                    propertyVector->setNull(i, true);
                }
            }
        }
    }
    
    return true;
}


} // namespace processor
} // namespace kuzu 