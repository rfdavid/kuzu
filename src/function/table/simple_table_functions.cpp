#include "function/table/simple_table_functions.h"

using namespace kuzu::common;

namespace kuzu {
namespace function {

SimpleTableFuncSharedState::~SimpleTableFuncSharedState() = default;

SimpleTableFuncMorsel SimpleTableFuncSharedState::getMorsel() {
    std::lock_guard lck{mtx};
    KU_ASSERT(curOffset <= maxOffset);
    if (curOffset == maxOffset) {
        return SimpleTableFuncMorsel::createInvalidMorsel();
    } else {
        const auto numValuesToOutput = std::min(DEFAULT_VECTOR_CAPACITY, maxOffset - curOffset);
        curOffset += numValuesToOutput;
        return {curOffset - numValuesToOutput, curOffset};
    }
}

std::unique_ptr<TableFuncSharedState> SimpleTableFunction::initSharedState(
    const TableFunctionInitInput& input) {
    const auto bindData = ku_dynamic_cast<SimpleTableFuncBindData*>(input.bindData);
    return std::make_unique<SimpleTableFuncSharedState>(bindData->maxOffset);
}

std::unique_ptr<TableFuncLocalState> SimpleTableFunction::initEmptyLocalState(
    const TableFunctionInitInput&, TableFuncSharedState*, storage::MemoryManager*) {
    return std::make_unique<TableFuncLocalState>();
}

} // namespace function
} // namespace kuzu
