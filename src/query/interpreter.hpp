#pragma once

#include <ctime>

#include "database/graph_db_accessor.hpp"
#include "query/context.hpp"
#include "query/frontend/ast/cypher_main_visitor.hpp"
#include "query/frontend/interpret/interpret.hpp"
#include "query/frontend/logical/planner.hpp"
#include "query/frontend/opencypher/parser.hpp"
#include "query/frontend/semantic/symbol_generator.hpp"

namespace query {

template <typename Stream>
void Interpret(const std::string &query, GraphDbAccessor &db_accessor,
               Stream &stream) {

  clock_t start_time = clock();

  Config config;
  Context ctx(config, db_accessor);
  std::map<std::string, TypedValue> summary;

  // query -> AST
  ::frontend::opencypher::Parser parser(query);
  auto low_level_tree = parser.tree();

  // AST -> high level tree
  query::frontend::CypherMainVisitor visitor(ctx);
  visitor.visit(low_level_tree);
  auto high_level_tree = visitor.query();

  // symbol table fill
  SymbolTable symbol_table;
  SymbolGenerator symbol_generator(symbol_table);
  high_level_tree->Accept(symbol_generator);

  // high level tree -> logical plan
  auto logical_plan = MakeLogicalPlan(*high_level_tree, symbol_table);

  // generate frame based on symbol table max_position
  Frame frame(symbol_table.max_position());

  if (auto produce = dynamic_cast<Produce *>(logical_plan.get())) {
    // top level node in the operator tree is a produce (return)
    // so stream out results

    // generate header
    std::vector<std::string> header;
    for (auto named_expression : produce->named_expressions())
      header.push_back(named_expression->name_);
    stream.Header(header);

    // collect the symbols from the return clause
    std::vector<Symbol> symbols;
    for (auto named_expression : produce->named_expressions())
      symbols.emplace_back(symbol_table[*named_expression]);

    // stream out results
    auto cursor = produce->MakeCursor(db_accessor);
    while (cursor->Pull(frame, symbol_table)) {
      std::vector<TypedValue> values;
      for (auto &symbol : symbols) values.emplace_back(frame[symbol]);
      stream.Result(values);
    }

    summary["type"] = "r";
  } else if (auto create = dynamic_cast<CreateNode *>(logical_plan.get())) {
    auto cursor = create->MakeCursor(db_accessor);
    while (cursor->Pull(frame, symbol_table)) {
      continue;
    }
  } else if (auto create = dynamic_cast<CreateExpand *>(logical_plan.get())) {
    auto cursor = create->MakeCursor(db_accessor);
    while (cursor->Pull(frame, symbol_table)) {
      continue;
    }
  }

  clock_t end_time = clock();
  double time_second = double(end_time - start_time) / CLOCKS_PER_SEC;
  summary["query_time_sec"] = TypedValue(time_second);
  stream.Summary(summary);

}
}
