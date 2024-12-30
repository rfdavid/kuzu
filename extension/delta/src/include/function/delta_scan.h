#pragma once

#include "binder/binder.h"
#include "connector/connector_factory.h"
#include "connector/delta_connector.h"
#include "connector/duckdb_result_converter.h"
#include "connector/duckdb_type_converter.h"
#include "function/table/scan_functions.h"
#include "function/table/simple_table_functions.h"

namespace kuzu {
namespace delta_extension {

struct DeltaScanFunction {
    static constexpr const char* name = "DELTA_SCAN";

    static function::function_set getFunctionSet();
};

struct DeltaScanBindData : public function::ScanBindData {
    std::string query;
    std::shared_ptr<duckdb_extension::DuckDBConnector> connector;
    duckdb_extension::DuckDBResultConverter converter;

    DeltaScanBindData(std::string query,
        std::shared_ptr<duckdb_extension::DuckDBConnector> connector,
        duckdb_extension::DuckDBResultConverter converter, binder::expression_vector columns,
        common::ReaderConfig config, main::ClientContext* ctx)
        : ScanBindData{std::move(columns), std::move(config), ctx}, query{std::move(query)},
          connector{std::move(connector)}, converter{std::move(converter)} {}

    std::unique_ptr<function::TableFuncBindData> copy() const override {
        return std::make_unique<DeltaScanBindData>(*this);
    }
};

// Functions and structs exposed for use
std::unique_ptr<function::TableFuncSharedState> initDeltaScanSharedState(
    function::TableFunctionInitInput& input);

std::unique_ptr<function::TableFuncLocalState> initEmptyLocalState(
    function::TableFunctionInitInput&, function::TableFuncSharedState*, storage::MemoryManager*);

common::offset_t tableFunc(function::TableFuncInput& input, function::TableFuncOutput& output);

} // namespace delta_extension
} // namespace kuzu