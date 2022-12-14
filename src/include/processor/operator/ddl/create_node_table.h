#pragma once

#include "processor/operator/ddl/create_table.h"
#include "storage/store/nodes_statistics_and_deleted_ids.h"

namespace kuzu {
namespace processor {

class CreateNodeTable : public CreateTable {
public:
    CreateNodeTable(Catalog* catalog, string tableName,
        vector<PropertyNameDataType> propertyNameDataTypes, uint32_t primaryKeyIdx, uint32_t id,
        const string& paramsString, NodesStatisticsAndDeletedIDs* nodesStatisticsAndDeletedIDs)
        : CreateTable{PhysicalOperatorType::CREATE_NODE_TABLE, catalog, std::move(tableName),
              std::move(propertyNameDataTypes), id, paramsString},
          primaryKeyIdx{primaryKeyIdx}, nodesStatisticsAndDeletedIDs{nodesStatisticsAndDeletedIDs} {
    }

    string execute() override;

    unique_ptr<PhysicalOperator> clone() override {
        return make_unique<CreateNodeTable>(catalog, tableName, propertyNameDataTypes,
            primaryKeyIdx, id, paramsString, nodesStatisticsAndDeletedIDs);
    }

private:
    uint32_t primaryKeyIdx;
    NodesStatisticsAndDeletedIDs* nodesStatisticsAndDeletedIDs;
};

} // namespace processor
} // namespace kuzu