#include "binder/binder.h"
#include "catalog/catalog.h"
#include "catalog/catalog_entry/node_table_catalog_entry.h"
#include "catalog/catalog_entry/rel_table_catalog_entry.h"
#include "common/exception/runtime.h"
#include "common/string_utils.h"
#include "function/table/bind_input.h"
#include "function/table/simple_table_functions.h"
#include "main/database_manager.h"

using namespace kuzu::catalog;
using namespace kuzu::common;

namespace kuzu {
namespace function {

struct TableInfoBindData final : SimpleTableFuncBindData {
    std::unique_ptr<TableCatalogEntry> catalogEntry;

    TableInfoBindData(std::unique_ptr<TableCatalogEntry> catalogEntry,
        binder::expression_vector columns, offset_t maxOffset)
        : SimpleTableFuncBindData{std::move(columns), maxOffset},
          catalogEntry{std::move(catalogEntry)} {}

    std::unique_ptr<TableFuncBindData> copy() const override {
        return std::make_unique<TableInfoBindData>(catalogEntry->copy(), columns, maxOffset);
    }
};

static offset_t tableFunc(const TableFuncInput& input, TableFuncOutput& output) {
    auto& dataChunk = output.dataChunk;
    auto sharedState = input.sharedState->ptrCast<SimpleTableFuncSharedState>();
    auto morsel = sharedState->getMorsel();
    if (!morsel.hasMoreToOutput()) {
        return 0;
    }
    auto bindData = input.bindData->constPtrCast<TableInfoBindData>();
    auto tableEntry = bindData->catalogEntry->constPtrCast<TableCatalogEntry>();
    auto numPropertiesToOutput = morsel.endOffset - morsel.startOffset;
    auto vectorPos = 0;
    for (auto i = 0u; i < numPropertiesToOutput; i++) {
        auto& property = tableEntry->getProperties()[morsel.startOffset + i];
        if (tableEntry->getTableType() == TableType::REL) {
            if (property.getName() == InternalKeyword::ID) {
                // Skip internal id column.
                continue;
            }
        }
        dataChunk.getValueVectorMutable(0).setValue(vectorPos,
            tableEntry->getPropertyIdx(property.getName()));
        dataChunk.getValueVectorMutable(1).setValue(vectorPos, property.getName());
        dataChunk.getValueVectorMutable(2).setValue(vectorPos, property.getType().toString());
        dataChunk.getValueVectorMutable(3).setValue(vectorPos, property.getDefaultExpressionName());

        if (tableEntry->getTableType() == TableType::NODE) {
            auto nodeTableEntry = tableEntry->constPtrCast<NodeTableCatalogEntry>();
            auto primaryKeyName = nodeTableEntry->getPrimaryKeyName();
            dataChunk.getValueVectorMutable(4).setValue(vectorPos,
                primaryKeyName == property.getName());
        } else if (tableEntry->getTableType() == TableType::REL) {
            const auto* relTableEntry = tableEntry->constPtrCast<RelTableCatalogEntry>();
            dataChunk.getValueVectorMutable(4).setValue(vectorPos,
                RelDirectionUtils::relStorageDirectionToString(
                    relTableEntry->getStorageDirection()));
        }
        vectorPos++;
    }
    return vectorPos;
}

static std::unique_ptr<TableCatalogEntry> getTableCatalogEntry(const main::ClientContext* context,
    const std::string& tableName) {
    auto transaction = context->getTransaction();
    auto tableInfo = common::StringUtils::split(tableName, ".");
    if (tableInfo.size() == 1) {
        auto tableID = context->getCatalog()->getTableID(transaction, tableName);
        return context->getCatalog()->getTableCatalogEntry(transaction, tableID)->copy();
    } else {
        auto catalogName = tableInfo[0];
        auto attachedTableName = tableInfo[1];
        auto attachedDatabase = context->getDatabaseManager()->getAttachedDatabase(catalogName);
        if (attachedDatabase == nullptr) {
            throw common::RuntimeException{
                common::stringFormat("Database: {} doesn't exist.", catalogName)};
        }
        auto tableID = attachedDatabase->getCatalog()->getTableID(transaction, attachedTableName);
        return attachedDatabase->getCatalog()->getTableCatalogEntry(transaction, tableID)->copy();
    }
}

static std::unique_ptr<TableFuncBindData> bindFunc(const main::ClientContext* context,
    const TableFuncBindInput* input) {
    std::vector<std::string> columnNames;
    std::vector<LogicalType> columnTypes;
    auto catalogEntry = getTableCatalogEntry(context, input->getLiteralVal<std::string>(0));
    const auto tableEntry = catalogEntry->constPtrCast<TableCatalogEntry>();
    columnNames.emplace_back("property id");
    columnTypes.push_back(LogicalType::INT32());
    columnNames.emplace_back("name");
    columnTypes.push_back(LogicalType::STRING());
    columnNames.emplace_back("type");
    columnTypes.push_back(LogicalType::STRING());
    columnNames.emplace_back("default expression");
    columnTypes.push_back(LogicalType::STRING());
    if (tableEntry->getTableType() == TableType::NODE) {
        columnNames.emplace_back("primary key");
        columnTypes.push_back(LogicalType::BOOL());
    } else if (tableEntry->getTableType() == TableType::REL) {
        columnNames.emplace_back("storage_direction");
        columnTypes.push_back(LogicalType::STRING());
    }
    auto columns = input->binder->createVariables(columnNames, columnTypes);
    return std::make_unique<TableInfoBindData>(std::move(catalogEntry), columns,
        tableEntry->getNumProperties());
}

function_set TableInfoFunction::getFunctionSet() {
    function_set functionSet;
    auto function = std::make_unique<TableFunction>(name, std::vector{LogicalTypeID::STRING});
    function->tableFunc = tableFunc;
    function->bindFunc = bindFunc;
    function->initSharedStateFunc = initSharedState;
    function->initLocalStateFunc = initEmptyLocalState;
    functionSet.push_back(std::move(function));
    return functionSet;
}

} // namespace function
} // namespace kuzu
