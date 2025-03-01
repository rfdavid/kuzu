#include "processor/operator/scan/scan_table.h"
#include "storage/store/rel_table.h"


namespace kuzu {
namespace processor {

void ScanTable::initVectors(storage::TableScanState& state, const ResultSet& resultSet) const {
    state.nodeIDVector = resultSet.getValueVector(info.nodeIDPos).get();

    auto* relTableScanState = dynamic_cast<storage::RelTableScanState*>(&state);
    if (relTableScanState) {
        // TODO (Rui): fix this to properly map the ids
        state.outputVectors.push_back(resultSet.getValueVector(info.outVectorsPos[0]).get());
        state.outputVectors.push_back(resultSet.getValueVector(info.outVectorsPos[2]).get());
//        size_t count = std::min(size_t(2), info.outVectorsPos.size());
//        for (size_t i = 0; i < count; ++i) {
//            state.outputVectors.push_back(resultSet.getValueVector(info.outVectorsPos[i]).get());
 //       }
    } else {
        for (auto& pos : info.outVectorsPos) {
            state.outputVectors.push_back(resultSet.getValueVector(pos).get());
        }
    }
}

} // namespace processor
} // namespace kuzu
