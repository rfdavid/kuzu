#include "function/query_fts_index.h"

#include "binder/binder.h"
#include "binder/expression/expression_util.h"
#include "binder/expression/literal_expression.h"
#include "catalog/fts_index_catalog_entry.h"
#include "common/task_system/task_scheduler.h"
#include "common/types/internal_id_util.h"
#include "function/fts_utils.h"
#include "function/gds/gds.h"
#include "function/gds/gds_frontier.h"
#include "function/gds/gds_task.h"
#include "function/gds/gds_utils.h"
#include "function/stem.h"
#include "graph/on_disk_graph.h"
#include "libstemmer.h"
#include "processor/execution_context.h"
#include "processor/result/factorized_table.h"
#include "re2.h"
#include "storage/index/index_utils.h"
#include "storage/storage_manager.h"

using namespace kuzu::binder;
using namespace kuzu::common;

namespace kuzu {
namespace fts_extension {

using namespace function;

struct QueryFTSBindData final : GDSBindData {
    std::vector<std::string> terms;
    uint64_t numDocs;
    double_t avgDocLen;
    QueryFTSConfig config;
    table_id_t outputTableID;

    QueryFTSBindData(std::vector<std::string> terms, graph::GraphEntry graphEntry,
        std::shared_ptr<Expression> docs, uint64_t numDocs, double_t avgDocLen,
        QueryFTSConfig config)
        : GDSBindData{std::move(graphEntry), std::move(docs)}, terms{std::move(terms)},
          numDocs{numDocs}, avgDocLen{avgDocLen}, config{config},
          outputTableID{nodeOutput->constCast<NodeExpression>().getSingleEntry()->getTableID()} {}
    QueryFTSBindData(const QueryFTSBindData& other)
        : GDSBindData{other}, terms{other.terms}, numDocs{other.numDocs},
          avgDocLen{other.avgDocLen}, config{other.config}, outputTableID{other.outputTableID} {}

    bool hasNodeInput() const override { return false; }

    uint64_t getNumUniqueTerms() const;

    std::unique_ptr<GDSBindData> copy() const override {
        return std::make_unique<QueryFTSBindData>(*this);
    }
};

uint64_t QueryFTSBindData::getNumUniqueTerms() const {
    auto uniqueTerms = std::unordered_set<std::string>{terms.begin(), terms.end()};
    return uniqueTerms.size();
}

struct ScoreData {
    uint64_t df;
    uint64_t tf;

    ScoreData(uint64_t df, uint64_t tf) : df{df}, tf{tf} {}
};

struct ScoreInfo {
    std::vector<ScoreData> scoreData;

    void addEdge(uint64_t df, uint64_t tf) { scoreData.emplace_back(df, tf); }
};

struct QFTSEdgeCompute final : EdgeCompute {
    DoublePathLengthsFrontierPair* termsFrontier;
    node_id_map_t<ScoreInfo>* scores;
    const node_id_map_t<uint64_t>& dfs;

    QFTSEdgeCompute(DoublePathLengthsFrontierPair* termsFrontier, node_id_map_t<ScoreInfo>* scores,
        const node_id_map_t<uint64_t>& dfs)
        : termsFrontier{termsFrontier}, scores{scores}, dfs{dfs} {}

    std::vector<nodeID_t> edgeCompute(nodeID_t boundNodeID, graph::NbrScanState::Chunk& resultChunk,
        bool) override;

    std::unique_ptr<EdgeCompute> copy() override {
        return std::make_unique<QFTSEdgeCompute>(termsFrontier, scores, dfs);
    }
};

std::vector<nodeID_t> QFTSEdgeCompute::edgeCompute(nodeID_t boundNodeID,
    graph::NbrScanState::Chunk& resultChunk, bool) {
    KU_ASSERT(dfs.contains(boundNodeID));
    std::vector<nodeID_t> activeNodes;
    resultChunk.forEach<uint64_t>([&](auto docNodeID, auto /* edgeID */, auto tf) {
        auto df = dfs.at(boundNodeID);
        if (!scores->contains(docNodeID)) {
            scores->emplace(docNodeID, ScoreInfo{});
        }
        scores->at(docNodeID).addEdge(df, tf);
        activeNodes.push_back(docNodeID);
    });
    return activeNodes;
}

struct QFTSOutput {
    node_id_map_t<ScoreInfo> scores;

    QFTSOutput() = default;
    virtual ~QFTSOutput() = default;
};

struct QFTSState final : GDSComputeState {
    void initFirstFrontierWithTerms(const node_id_map_t<uint64_t>& dfs,
        table_id_t termsTableID) const;

    QFTSState(std::unique_ptr<FrontierPair> frontierPair, std::unique_ptr<EdgeCompute> edgeCompute,
        table_id_t termsTableID);
};

QFTSState::QFTSState(std::unique_ptr<FrontierPair> frontierPair,
    std::unique_ptr<EdgeCompute> edgeCompute, table_id_t termsTableID)
    : GDSComputeState{std::move(frontierPair), std::move(edgeCompute),
          nullptr /* outputNodeMask */} {
    this->frontierPair->pinNextFrontier(termsTableID);
}

void QFTSState::initFirstFrontierWithTerms(const node_id_map_t<uint64_t>& dfs,
    table_id_t termsTableID) const {
    SparseFrontier sparseFrontier;
    sparseFrontier.pinTableID(termsTableID);
    for (auto& [nodeID, df] : dfs) {
        frontierPair->addNodeToNextDenseFrontier(nodeID);
        sparseFrontier.addNode(nodeID);
        sparseFrontier.checkSampleSize();
    }
    frontierPair->mergeLocalFrontier(sparseFrontier);
}

void runFrontiersOnce(processor::ExecutionContext* executionContext, QFTSState& qFtsState,
    graph::Graph* graph, const std::string& tfProperty) {
    auto frontierPair = qFtsState.frontierPair.get();
    frontierPair->beginNewIteration();
    auto infos = graph->getRelFromToEntryInfos();
    KU_ASSERT(infos.size() == 1);
    auto& info = infos[0];
    frontierPair->beginFrontierComputeBetweenTables(info.fromEntry->getTableID(),
        info.toEntry->getTableID());
    GDSUtils::scheduleFrontierTask(info.fromEntry, info.toEntry, info.relEntry, graph,
        ExtendDirection::FWD, qFtsState, executionContext, 1 /* numThreads */, tfProperty);
}

class QFTSOutputWriter {
public:
    QFTSOutputWriter(storage::MemoryManager* mm, QFTSOutput* qFTSOutput,
        const QueryFTSBindData& bindData);

    void write(processor::FactorizedTable& scoreFT, nodeID_t docNodeID, uint64_t len,
        int64_t docsID);

    std::unique_ptr<QFTSOutputWriter> copy();

private:
    QFTSOutput* qFTSOutput;
    ValueVector docsVector;
    ValueVector scoreVector;
    std::vector<ValueVector*> vectors;
    idx_t pos;
    storage::MemoryManager* mm;
    const QueryFTSBindData& bindData;
    uint64_t numUniqueTerms;
};

QFTSOutputWriter::QFTSOutputWriter(storage::MemoryManager* mm, QFTSOutput* qFTSOutput,
    const QueryFTSBindData& bindData)
    : qFTSOutput{qFTSOutput}, docsVector{LogicalType::INTERNAL_ID(), mm},
      scoreVector{LogicalType::UINT64(), mm}, mm{mm}, bindData{bindData} {
    auto state = DataChunkState::getSingleValueDataChunkState();
    pos = state->getSelVector()[0];
    docsVector.setState(state);
    scoreVector.setState(state);
    docsVector.setNull(pos, false /* isNull */);
    scoreVector.setNull(pos, false /* isNull */);
    vectors.push_back(&docsVector);
    vectors.push_back(&scoreVector);
    numUniqueTerms = bindData.getNumUniqueTerms();
}

void QFTSOutputWriter::write(processor::FactorizedTable& scoreFT, nodeID_t docNodeID, uint64_t len,
    int64_t docsID) {
    if (!qFTSOutput->scores.contains(docNodeID)) {
        return;
    }
    auto k = bindData.config.k;
    auto b = bindData.config.b;

    auto scoreInfo = qFTSOutput->scores.at(docNodeID);
    double score = 0;
    // If the query is conjunctive, the numbers of distinct terms in the doc and the number of
    // distinct terms in the query must be equal to each other.
    if (bindData.config.isConjunctive && scoreInfo.scoreData.size() != numUniqueTerms) {
        return;
    }
    for (auto& scoreData : scoreInfo.scoreData) {
        auto numDocs = bindData.numDocs;
        auto avgDocLen = bindData.avgDocLen;
        auto df = scoreData.df;
        auto tf = scoreData.tf;
        score += log10((numDocs - df + 0.5) / (df + 0.5) + 1) *
                 ((tf * (k + 1) / (tf + k * (1 - b + b * (len / avgDocLen)))));
    }
    docsVector.setValue(pos, nodeID_t{static_cast<offset_t>(docsID), bindData.outputTableID});
    scoreVector.setValue(pos, score);
    scoreFT.append(vectors);
}

std::unique_ptr<QFTSOutputWriter> QFTSOutputWriter::copy() {
    return std::make_unique<QFTSOutputWriter>(mm, qFTSOutput, bindData);
}

class QFTSVertexCompute final : public VertexCompute {
public:
    explicit QFTSVertexCompute(storage::MemoryManager* mm,
        processor::GDSCallSharedState* sharedState, std::unique_ptr<QFTSOutputWriter> writer)
        : mm{mm}, sharedState{sharedState}, writer{std::move(writer)} {
        scoreFT = sharedState->claimLocalTable(mm);
    }

    ~QFTSVertexCompute() override { sharedState->returnLocalTable(scoreFT); }

    void vertexCompute(const graph::VertexScanState::Chunk& chunk) override;

    std::unique_ptr<VertexCompute> copy() override {
        return std::make_unique<QFTSVertexCompute>(mm, sharedState, writer->copy());
    }

private:
    storage::MemoryManager* mm;
    processor::GDSCallSharedState* sharedState;
    processor::FactorizedTable* scoreFT;
    std::unique_ptr<QFTSOutputWriter> writer;
};

void QFTSVertexCompute::vertexCompute(const graph::VertexScanState::Chunk& chunk) {
    auto docLens = chunk.getProperties<uint64_t>(0);
    auto docIDs = chunk.getProperties<int64_t>(1);
    for (auto i = 0u; i < chunk.getNodeIDs().size(); i++) {
        writer->write(*scoreFT, chunk.getNodeIDs()[i], docLens[i], docIDs[i]);
    }
}

static node_id_map_t<uint64_t> getDFs(main::ClientContext& context,
    const catalog::NodeTableCatalogEntry& termsEntry, const std::vector<std::string>& terms) {
    auto storageManager = context.getStorageManager();
    auto tableID = termsEntry.getTableID();
    auto& termsNodeTable = storageManager->getTable(tableID)->cast<storage::NodeTable>();
    auto tx = context.getTransaction();
    auto dfColumnID = termsEntry.getColumnID(QueryFTSAlgorithm::DOC_FREQUENCY_PROP_NAME);
    auto dfColumn = &termsNodeTable.getColumn(dfColumnID);
    auto nodeIDVector = std::make_shared<ValueVector>(LogicalType::INTERNAL_ID());
    auto dataChunk = DataChunk{2 /* numValueVectors */};
    dataChunk.state = DataChunkState::getSingleValueDataChunkState();
    auto dfVector = std::make_shared<ValueVector>(LogicalType::UINT64());
    dataChunk.insert(0, dfVector);
    dataChunk.insert(1, nodeIDVector);
    auto termsVector = ValueVector(LogicalType::STRING(), context.getMemoryManager());
    termsVector.state = dataChunk.state;
    auto nodeTableScanState = storage::NodeTableScanState{tableID, {dfColumnID}, {dfColumn},
        dataChunk, nodeIDVector.get()};
    node_id_map_t<uint64_t> dfs;
    for (auto& term : terms) {
        termsVector.setValue(0, term);
        common::offset_t offset = 0;
        if (!termsNodeTable.lookupPK(tx, &termsVector, 0, offset)) {
            continue;
        }
        auto nodeID = nodeID_t{offset, tableID};
        nodeIDVector->setValue(0, nodeID);
        termsNodeTable.initScanState(tx, nodeTableScanState, tableID, offset);
        termsNodeTable.lookup(tx, nodeTableScanState);
        dfs.emplace(nodeID, dfVector->getValue<uint64_t>(0));
    }
    return dfs;
}

void QueryFTSAlgorithm::exec(processor::ExecutionContext* executionContext) {
    auto graphEntry = sharedState->graph->getGraphEntry();
    auto output = std::make_unique<QFTSOutput>();
    auto qFTSBindData = bindData->ptrCast<QueryFTSBindData>();
    auto& termsEntry = graphEntry->nodeEntries[0]->constCast<catalog::NodeTableCatalogEntry>();
    auto dfs = getDFs(*executionContext->clientContext, termsEntry, qFTSBindData->terms);
    // Do edge compute to extend terms -> docs and save the term frequency and document frequency
    // for each term-doc pair. The reason why we store the term frequency and document frequency
    // is that: we need the `len` property from the docs table which is only available during the
    // vertex compute.
    auto currentFrontier = getPathLengthsFrontier(executionContext, PathLengths::UNVISITED);
    auto nextFrontier = getPathLengthsFrontier(executionContext, PathLengths::UNVISITED);
    auto frontierPair =
        std::make_unique<DoublePathLengthsFrontierPair>(currentFrontier, nextFrontier);
    auto edgeCompute = std::make_unique<QFTSEdgeCompute>(frontierPair.get(), &output->scores, dfs);

    auto mm = executionContext->clientContext->getMemoryManager();
    QFTSState qFTSState =
        QFTSState{std::move(frontierPair), std::move(edgeCompute), termsEntry.getTableID()};
    qFTSState.initFirstFrontierWithTerms(dfs, termsEntry.getTableID());
    runFrontiersOnce(executionContext, qFTSState, sharedState->graph.get(),
        QueryFTSAlgorithm::TERM_FREQUENCY_PROP_NAME);

    // Do vertex compute to calculate the score for doc with the length property.
    auto writer = std::make_unique<QFTSOutputWriter>(mm, output.get(),
        *bindData->ptrCast<QueryFTSBindData>());
    auto writerVC = std::make_unique<QFTSVertexCompute>(mm, sharedState.get(), std::move(writer));
    auto docsEntry = graphEntry->nodeEntries[1];
    GDSUtils::runVertexCompute(executionContext, sharedState->graph.get(), *writerVC, docsEntry,
        {QueryFTSAlgorithm::DOC_LEN_PROP_NAME, QueryFTSAlgorithm::DOC_ID_PROP_NAME});
    sharedState->mergeLocalTables();
}

static std::shared_ptr<Expression> getScoreColumn(Binder* binder) {
    return binder->createVariable(QueryFTSAlgorithm::SCORE_PROP_NAME, LogicalType::DOUBLE());
}

expression_vector QueryFTSAlgorithm::getResultColumns(Binder* binder) const {
    expression_vector columns;
    auto& docsNode = bindData->getNodeOutput()->constCast<NodeExpression>();
    columns.push_back(docsNode.getInternalID());
    columns.push_back(getScoreColumn(binder));
    return columns;
}

static std::string getParamVal(const GDSBindInput& input, idx_t idx) {
    return ExpressionUtil::getLiteralValue<std::string>(
        input.getParam(idx)->constCast<LiteralExpression>());
}

static std::vector<std::string> getTerms(std::string& query, const std::string& stemmer) {
    std::string regexPattern = "[0-9!@#$%^&*()_+={}\\[\\]:;<>,.?~\\/\\|'\"`-]+";
    std::string replacePattern = " ";
    RE2::GlobalReplace(&query, regexPattern, replacePattern);
    StringUtils::toLower(query);
    auto terms = StringUtils::split(query, " ");
    if (stemmer == "none") {
        return terms;
    }
    StemFunction::validateStemmer(stemmer);
    auto sbStemmer = sb_stemmer_new(reinterpret_cast<const char*>(stemmer.c_str()), "UTF_8");
    std::vector<std::string> result;
    for (auto& term : terms) {
        auto stemData = sb_stemmer_stem(sbStemmer, reinterpret_cast<const sb_symbol*>(term.c_str()),
            term.length());
        result.push_back(reinterpret_cast<const char*>(stemData));
    }
    sb_stemmer_delete(sbStemmer);
    return result;
}

void QueryFTSAlgorithm::bind(const GDSBindInput& input, main::ClientContext& context) {
    context.setToUseInternalCatalogEntry();
    auto inputTableName = getParamVal(input, 0);
    auto indexName = getParamVal(input, 1);
    auto query = getParamVal(input, 2);

    auto tableEntry = storage::IndexUtils::bindTable(context, inputTableName, indexName,
        storage::IndexOperation::QUERY);
    auto& ftsIndexEntry =
        context.getCatalog()
            ->getIndex(context.getTransaction(), tableEntry->getTableID(), indexName)
            ->constCast<FTSIndexCatalogEntry>();
    auto terms = getTerms(query, ftsIndexEntry.getFTSConfig().stemmer);
    auto entry =
        context.getCatalog()->getTableCatalogEntry(context.getTransaction(), inputTableName);
    auto nodeOutput = bindNodeOutput(input.binder, {entry});

    auto termsEntry = context.getCatalog()->getTableCatalogEntry(context.getTransaction(),
        FTSUtils::getTermsTableName(tableEntry->getTableID(), indexName));
    auto docsEntry = context.getCatalog()->getTableCatalogEntry(context.getTransaction(),
        FTSUtils::getDocsTableName(tableEntry->getTableID(), indexName));
    auto appearsInEntry = context.getCatalog()->getTableCatalogEntry(context.getTransaction(),
        FTSUtils::getAppearsInTableName(tableEntry->getTableID(), indexName));
    auto graphEntry = graph::GraphEntry({termsEntry, docsEntry}, {appearsInEntry});
    bindData = std::make_unique<QueryFTSBindData>(std::move(terms), std::move(graphEntry),
        nodeOutput, ftsIndexEntry.getNumDocs(), ftsIndexEntry.getAvgDocLen(),
        QueryFTSConfig{input.optionalParams});
}

function_set QueryFTSFunction::getFunctionSet() {
    function_set result;
    auto algo = std::make_unique<QueryFTSAlgorithm>();
    result.push_back(
        std::make_unique<GDSFunction>(name, algo->getParameterTypeIDs(), std::move(algo)));
    return result;
}

} // namespace fts_extension
} // namespace kuzu
