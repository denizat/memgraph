#include "query/plan/rule_based_planner.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <stack>
#include <unordered_set>

#include "utils/algorithm.hpp"
#include "utils/exceptions.hpp"
#include "utils/flag_validation.hpp"

DEFINE_VALIDATED_HIDDEN_int64(
    query_vertex_count_to_expand_existing, 10,
    "Maximum count of indexed vertices which provoke "
    "indexed lookup and then expand to existing, instead of "
    "a regular expand. Default is 10, to turn off use -1.",
    FLAG_IN_RANGE(-1, std::numeric_limits<std::int64_t>::max()));

namespace query::plan {

namespace {

/// Utility function for iterating pattern atoms and accumulating a result.
///
/// Each pattern is of the form `NodeAtom (, EdgeAtom, NodeAtom)*`. Therefore,
/// the `base` function is called on the first `NodeAtom`, while the `collect`
/// is called for the whole triplet. Result of the function is passed to the
/// next call. Final result is returned.
///
/// Example usage of counting edge atoms in the pattern.
///
///    auto base = [](NodeAtom *first_node) { return 0; };
///    auto collect = [](int accum, NodeAtom *prev_node, EdgeAtom *edge,
///                      NodeAtom *node) {
///      return accum + 1;
///    };
///    int edge_count = ReducePattern<int>(pattern, base, collect);
///
// TODO: It might be a good idea to move this somewhere else, for easier usage
// in other files.
template <typename T>
auto ReducePattern(
    Pattern &pattern, std::function<T(NodeAtom *)> base,
    std::function<T(T, NodeAtom *, EdgeAtom *, NodeAtom *)> collect) {
  DCHECK(!pattern.atoms_.empty()) << "Missing atoms in pattern";
  auto atoms_it = pattern.atoms_.begin();
  auto current_node = dynamic_cast<NodeAtom *>(*atoms_it++);
  DCHECK(current_node) << "First pattern atom is not a node";
  auto last_res = base(current_node);
  // Remaining atoms need to follow sequentially as (EdgeAtom, NodeAtom)*
  while (atoms_it != pattern.atoms_.end()) {
    auto edge = dynamic_cast<EdgeAtom *>(*atoms_it++);
    DCHECK(edge) << "Expected an edge atom in pattern.";
    DCHECK(atoms_it != pattern.atoms_.end())
        << "Edge atom should not end the pattern.";
    auto prev_node = current_node;
    current_node = dynamic_cast<NodeAtom *>(*atoms_it++);
    DCHECK(current_node) << "Expected a node atom in pattern.";
    last_res = collect(std::move(last_res), prev_node, edge, current_node);
  }
  return last_res;
}

auto GenCreate(Create &create, std::unique_ptr<LogicalOperator> input_op,
               const SymbolTable &symbol_table,
               std::unordered_set<Symbol> &bound_symbols) {
  auto last_op = std::move(input_op);
  for (auto pattern : create.patterns_) {
    last_op = impl::GenCreateForPattern(*pattern, std::move(last_op),
                                        symbol_table, bound_symbols);
  }
  return last_op;
}

bool HasBoundFilterSymbols(const std::unordered_set<Symbol> &bound_symbols,
                           const FilterInfo &filter) {
  for (const auto &symbol : filter.used_symbols) {
    if (bound_symbols.find(symbol) == bound_symbols.end()) {
      return false;
    }
  }
  return true;
}

// Ast tree visitor which collects the context for a return body.
// The return body of WITH and RETURN clauses consists of:
//
//   * named expressions (used to produce results);
//   * flag whether the results need to be DISTINCT;
//   * optional SKIP expression;
//   * optional LIMIT expression and
//   * optional ORDER BY expressions.
//
// In addition to the above, we collect information on used symbols,
// aggregations and expressions used for group by.
class ReturnBodyContext : public HierarchicalTreeVisitor {
 public:
  ReturnBodyContext(const ReturnBody &body, SymbolTable &symbol_table,
                    const std::unordered_set<Symbol> &bound_symbols,
                    AstTreeStorage &storage, Where *where = nullptr)
      : body_(body),
        symbol_table_(symbol_table),
        bound_symbols_(bound_symbols),
        storage_(storage),
        where_(where) {
    // Collect symbols from named expressions.
    output_symbols_.reserve(body_.named_expressions.size());
    if (body.all_identifiers) {
      // Expand '*' to expressions and symbols first, so that their results come
      // before regular named expressions.
      ExpandUserSymbols();
    }
    for (auto &named_expr : body_.named_expressions) {
      output_symbols_.emplace_back(symbol_table_.at(*named_expr));
      named_expr->Accept(*this);
      named_expressions_.emplace_back(named_expr);
    }
    // Collect symbols used in group by expressions.
    if (!aggregations_.empty()) {
      UsedSymbolsCollector collector(symbol_table_);
      for (auto &group_by : group_by_) {
        group_by->Accept(collector);
      }
      group_by_used_symbols_ = collector.symbols_;
    }
    if (aggregations_.empty()) {
      // Visit order_by and where if we do not have aggregations. This way we
      // prevent collecting group_by expressions from order_by and where, which
      // would be very wrong. When we have aggregation, order_by and where can
      // only use new symbols (ensured in semantic analysis), so we don't care
      // about collecting used_symbols. Also, semantic analysis should
      // have prevented any aggregations from appearing here.
      for (const auto &order_pair : body.order_by) {
        order_pair.second->Accept(*this);
      }
      if (where) {
        where->Accept(*this);
      }
      DCHECK(aggregations_.empty())
          << "Unexpected aggregations in ORDER BY or WHERE";
    }
  }

  using HierarchicalTreeVisitor::PostVisit;
  using HierarchicalTreeVisitor::PreVisit;
  using HierarchicalTreeVisitor::Visit;

  bool Visit(PrimitiveLiteral &) override {
    has_aggregation_.emplace_back(false);
    return true;
  }

 private:
  template <typename TLiteral, typename TIteratorToExpression>
  void PostVisitCollectionLiteral(
      TLiteral &literal, TIteratorToExpression iterator_to_expression) {
    // If there is an aggregation in the list, and there are group-bys, then we
    // need to add the group-bys manually. If there are no aggregations, the
    // whole list will be added as a group-by.
    std::vector<Expression *> literal_group_by;
    bool has_aggr = false;
    auto it = has_aggregation_.end();
    auto elements_it = literal.elements_.begin();
    std::advance(it, -literal.elements_.size());
    while (it != has_aggregation_.end()) {
      if (*it) {
        has_aggr = true;
      } else {
        literal_group_by.emplace_back(iterator_to_expression(elements_it));
      }
      elements_it++;
      it = has_aggregation_.erase(it);
    }
    has_aggregation_.emplace_back(has_aggr);
    if (has_aggr) {
      for (auto expression_ptr : literal_group_by)
        group_by_.emplace_back(expression_ptr);
    }
  }

 public:
  bool PostVisit(ListLiteral &list_literal) override {
    DCHECK(list_literal.elements_.size() <= has_aggregation_.size())
        << "Expected has_aggregation_ flags as much as there are list "
           "elements.";
    PostVisitCollectionLiteral(list_literal, [](auto it) { return *it; });
    return true;
  }

  bool PostVisit(MapLiteral &map_literal) override {
    DCHECK(map_literal.elements_.size() <= has_aggregation_.size())
        << "Expected has_aggregation_ flags as much as there are map elements.";
    PostVisitCollectionLiteral(map_literal, [](auto it) { return it->second; });
    return true;
  }

  bool PostVisit(All &all) override {
    // Remove the symbol which is bound by all, because we are only interested
    // in free (unbound) symbols.
    used_symbols_.erase(symbol_table_.at(*all.identifier_));
    DCHECK(has_aggregation_.size() >= 3U)
        << "Expected 3 has_aggregation_ flags for ALL arguments";
    bool has_aggr = false;
    for (int i = 0; i < 3; ++i) {
      has_aggr = has_aggr || has_aggregation_.back();
      has_aggregation_.pop_back();
    }
    has_aggregation_.emplace_back(has_aggr);
    return true;
  }

  bool PostVisit(Single &single) override {
    // Remove the symbol which is bound by single, because we are only
    // interested in free (unbound) symbols.
    used_symbols_.erase(symbol_table_.at(*single.identifier_));
    DCHECK(has_aggregation_.size() >= 3U)
        << "Expected 3 has_aggregation_ flags for SINGLE arguments";
    bool has_aggr = false;
    for (int i = 0; i < 3; ++i) {
      has_aggr = has_aggr || has_aggregation_.back();
      has_aggregation_.pop_back();
    }
    has_aggregation_.emplace_back(has_aggr);
    return true;
  }

  bool PostVisit(Reduce &reduce) override {
    // Remove the symbols bound by reduce, because we are only interested
    // in free (unbound) symbols.
    used_symbols_.erase(symbol_table_.at(*reduce.accumulator_));
    used_symbols_.erase(symbol_table_.at(*reduce.identifier_));
    DCHECK(has_aggregation_.size() >= 5U)
        << "Expected 5 has_aggregation_ flags for REDUCE arguments";
    bool has_aggr = false;
    for (int i = 0; i < 5; ++i) {
      has_aggr = has_aggr || has_aggregation_.back();
      has_aggregation_.pop_back();
    }
    has_aggregation_.emplace_back(has_aggr);
    return true;
  }

  bool Visit(Identifier &ident) override {
    const auto &symbol = symbol_table_.at(ident);
    if (!utils::Contains(output_symbols_, symbol)) {
      // Don't pick up new symbols, even though they may be used in ORDER BY or
      // WHERE.
      used_symbols_.insert(symbol);
    }
    has_aggregation_.emplace_back(false);
    return true;
  }

  bool PreVisit(ListSlicingOperator &list_slicing) override {
    list_slicing.list_->Accept(*this);
    bool list_has_aggr = has_aggregation_.back();
    has_aggregation_.pop_back();
    bool has_aggr = list_has_aggr;
    if (list_slicing.lower_bound_) {
      list_slicing.lower_bound_->Accept(*this);
      has_aggr = has_aggr || has_aggregation_.back();
      has_aggregation_.pop_back();
    }
    if (list_slicing.upper_bound_) {
      list_slicing.upper_bound_->Accept(*this);
      has_aggr = has_aggr || has_aggregation_.back();
      has_aggregation_.pop_back();
    }
    if (has_aggr && !list_has_aggr) {
      // We need to group by the list expression, because it didn't have an
      // aggregation inside.
      group_by_.emplace_back(list_slicing.list_);
    }
    has_aggregation_.emplace_back(has_aggr);
    return false;
  }

  bool PreVisit(IfOperator &if_operator) override {
    if_operator.condition_->Accept(*this);
    bool has_aggr = has_aggregation_.back();
    has_aggregation_.pop_back();
    if_operator.then_expression_->Accept(*this);
    has_aggr = has_aggr || has_aggregation_.back();
    has_aggregation_.pop_back();
    if_operator.else_expression_->Accept(*this);
    has_aggr = has_aggr || has_aggregation_.back();
    has_aggregation_.pop_back();
    has_aggregation_.emplace_back(has_aggr);
    // TODO: Once we allow aggregations here, insert appropriate stuff in
    // group_by.
    DCHECK(!has_aggr) << "Currently aggregations in CASE are not allowed";
    return false;
  }

  bool PostVisit(Function &function) override {
    DCHECK(function.arguments_.size() <= has_aggregation_.size())
        << "Expected has_aggregation_ flags as much as there are "
           "function arguments.";
    bool has_aggr = false;
    auto it = has_aggregation_.end();
    std::advance(it, -function.arguments_.size());
    while (it != has_aggregation_.end()) {
      has_aggr = has_aggr || *it;
      it = has_aggregation_.erase(it);
    }
    has_aggregation_.emplace_back(has_aggr);
    return true;
  }

#define VISIT_BINARY_OPERATOR(BinaryOperator)                              \
  bool PostVisit(BinaryOperator &op) override {                            \
    DCHECK(has_aggregation_.size() >= 2U)                                  \
        << "Expected at least 2 has_aggregation_ flags.";                  \
    /* has_aggregation_ stack is reversed, last result is from the 2nd */  \
    /* expression. */                                                      \
    bool aggr2 = has_aggregation_.back();                                  \
    has_aggregation_.pop_back();                                           \
    bool aggr1 = has_aggregation_.back();                                  \
    has_aggregation_.pop_back();                                           \
    bool has_aggr = aggr1 || aggr2;                                        \
    if (has_aggr && !(aggr1 && aggr2)) {                                   \
      /* Group by the expression which does not contain aggregation. */    \
      /* Possible optimization is to ignore constant value expressions */  \
      group_by_.emplace_back(aggr1 ? op.expression2_ : op.expression1_);   \
    }                                                                      \
    /* Propagate that this whole expression may contain an aggregation. */ \
    has_aggregation_.emplace_back(has_aggr);                               \
    return true;                                                           \
  }

  VISIT_BINARY_OPERATOR(OrOperator)
  VISIT_BINARY_OPERATOR(XorOperator)
  VISIT_BINARY_OPERATOR(AndOperator)
  VISIT_BINARY_OPERATOR(AdditionOperator)
  VISIT_BINARY_OPERATOR(SubtractionOperator)
  VISIT_BINARY_OPERATOR(MultiplicationOperator)
  VISIT_BINARY_OPERATOR(DivisionOperator)
  VISIT_BINARY_OPERATOR(ModOperator)
  VISIT_BINARY_OPERATOR(NotEqualOperator)
  VISIT_BINARY_OPERATOR(EqualOperator)
  VISIT_BINARY_OPERATOR(LessOperator)
  VISIT_BINARY_OPERATOR(GreaterOperator)
  VISIT_BINARY_OPERATOR(LessEqualOperator)
  VISIT_BINARY_OPERATOR(GreaterEqualOperator)
  VISIT_BINARY_OPERATOR(InListOperator)
  VISIT_BINARY_OPERATOR(ListMapIndexingOperator)

#undef VISIT_BINARY_OPERATOR

  bool PostVisit(Aggregation &aggr) override {
    // Aggregation contains a virtual symbol, where the result will be stored.
    const auto &symbol = symbol_table_.at(aggr);
    aggregations_.emplace_back(Aggregate::Element{
        aggr.expression1_, aggr.expression2_, aggr.op_, symbol});
    // Aggregation expression1_ is optional in COUNT(*), and COLLECT_MAP uses
    // two expressions, so we can have 0, 1 or 2 elements on the
    // has_aggregation_stack for this Aggregation expression.
    if (aggr.op_ == Aggregation::Op::COLLECT_MAP) has_aggregation_.pop_back();
    if (aggr.expression1_)
      has_aggregation_.back() = true;
    else
      has_aggregation_.emplace_back(true);
    // Possible optimization is to skip remembering symbols inside aggregation.
    // If and when implementing this, don't forget that Accumulate needs *all*
    // the symbols, including those inside aggregation.
    return true;
  }

  bool PostVisit(NamedExpression &named_expr) override {
    DCHECK(has_aggregation_.size() == 1U)
        << "Expected to reduce has_aggregation_ to single boolean.";
    if (!has_aggregation_.back()) {
      group_by_.emplace_back(named_expr.expression_);
    }
    has_aggregation_.pop_back();
    return true;
  }

  bool Visit(ParameterLookup &) override {
    has_aggregation_.emplace_back(false);
    return true;
  }

  bool Visit(query::CreateIndex &) override {
    has_aggregation_.emplace_back(false);
    return true;
  }

  // Creates NamedExpression with an Identifier for each user declared symbol.
  // This should be used when body.all_identifiers is true, to generate
  // expressions for Produce operator.
  void ExpandUserSymbols() {
    DCHECK(named_expressions_.empty())
        << "ExpandUserSymbols should be first to fill named_expressions_";
    DCHECK(output_symbols_.empty())
        << "ExpandUserSymbols should be first to fill output_symbols_";
    for (const auto &symbol : bound_symbols_) {
      if (!symbol.user_declared()) {
        continue;
      }
      auto *ident = storage_.Create<Identifier>(symbol.name());
      symbol_table_[*ident] = symbol;
      auto *named_expr = storage_.Create<NamedExpression>(symbol.name(), ident);
      symbol_table_[*named_expr] = symbol;
      // Fill output expressions and symbols with expanded identifiers.
      named_expressions_.emplace_back(named_expr);
      output_symbols_.emplace_back(symbol);
      used_symbols_.insert(symbol);
      // Don't forget to group by expanded identifiers.
      group_by_.emplace_back(ident);
    }
    // Cypher RETURN/WITH * expects to expand '*' sorted by name.
    std::sort(output_symbols_.begin(), output_symbols_.end(),
              [](const auto &a, const auto &b) { return a.name() < b.name(); });
    std::sort(named_expressions_.begin(), named_expressions_.end(),
              [](const auto &a, const auto &b) { return a->name_ < b->name_; });
  }

  // If true, results need to be distinct.
  bool distinct() const { return body_.distinct; }
  // Named expressions which are used to produce results.
  const auto &named_expressions() const { return named_expressions_; }
  // Pairs of (Ordering, Expression *) for sorting results.
  const auto &order_by() const { return body_.order_by; }
  // Optional expression which determines how many results to skip.
  auto *skip() const { return body_.skip; }
  // Optional expression which determines how many results to produce.
  auto *limit() const { return body_.limit; }
  // Optional Where clause for filtering.
  const auto *where() const { return where_; }
  // Set of symbols used inside the visited expressions, including the inside of
  // aggregation expression. These only includes old symbols, even though new
  // ones may have been used in ORDER BY or WHERE.
  const auto &used_symbols() const { return used_symbols_; }
  // List of aggregation elements found in expressions.
  const auto &aggregations() const { return aggregations_; }
  // When there is at least one aggregation element, all the non-aggregate (sub)
  // expressions are used for grouping. For example, in `WITH sum(n.a) + 2 * n.b
  // AS sum, n.c AS nc`, we will group by `2 * n.b` and `n.c`.
  const auto &group_by() const { return group_by_; }
  // Set of symbols used in group by expressions.
  const auto &group_by_used_symbols() const { return group_by_used_symbols_; }
  // All symbols generated by named expressions. They are collected in order of
  // named_expressions.
  const auto &output_symbols() const { return output_symbols_; }

 private:
  const ReturnBody &body_;
  SymbolTable &symbol_table_;
  const std::unordered_set<Symbol> &bound_symbols_;
  AstTreeStorage &storage_;
  const Where *const where_ = nullptr;
  std::unordered_set<Symbol> used_symbols_;
  std::vector<Symbol> output_symbols_;
  std::vector<Aggregate::Element> aggregations_;
  std::vector<Expression *> group_by_;
  std::unordered_set<Symbol> group_by_used_symbols_;
  // Flag stack indicating whether an expression contains an aggregation. A
  // stack is needed so that we differentiate the case where a child
  // sub-expression has an aggregation, while the other child doesn't. For
  // example AST, (+ (sum x) y)
  //   * (sum x) -- Has an aggregation.
  //   * y -- Doesn't, we need to group by this.
  //   * (+ (sum x) y) -- The whole expression has an aggregation, so we don't
  //                      group by it.
  std::list<bool> has_aggregation_;
  std::vector<NamedExpression *> named_expressions_;
};

std::unique_ptr<LogicalOperator> GenReturnBody(
    std::unique_ptr<LogicalOperator> input_op, bool advance_command,
    const ReturnBodyContext &body, bool accumulate = false) {
  std::vector<Symbol> used_symbols(body.used_symbols().begin(),
                                   body.used_symbols().end());
  auto last_op = std::move(input_op);
  if (accumulate) {
    // We only advance the command in Accumulate. This is done for WITH clause,
    // when the first part updated the database. RETURN clause may only need an
    // accumulation after updates, without advancing the command.
    last_op = std::make_unique<Accumulate>(std::move(last_op), used_symbols,
                                           advance_command);
  }
  if (!body.aggregations().empty()) {
    // When we have aggregation, SKIP/LIMIT should always come after it.
    std::vector<Symbol> remember(body.group_by_used_symbols().begin(),
                                 body.group_by_used_symbols().end());
    last_op = std::make_unique<Aggregate>(
        std::move(last_op), body.aggregations(), body.group_by(), remember);
  }
  last_op =
      std::make_unique<Produce>(std::move(last_op), body.named_expressions());
  // Distinct in ReturnBody only makes Produce values unique, so plan after it.
  if (body.distinct()) {
    last_op =
        std::make_unique<Distinct>(std::move(last_op), body.output_symbols());
  }
  // Like Where, OrderBy can read from symbols established by named expressions
  // in Produce, so it must come after it.
  if (!body.order_by().empty()) {
    last_op = std::make_unique<OrderBy>(std::move(last_op), body.order_by(),
                                        body.output_symbols());
  }
  // Finally, Skip and Limit must come after OrderBy.
  if (body.skip()) {
    last_op = std::make_unique<Skip>(std::move(last_op), body.skip());
  }
  // Limit is always after Skip.
  if (body.limit()) {
    last_op = std::make_unique<Limit>(std::move(last_op), body.limit());
  }
  // Where may see new symbols so it comes after we generate Produce and in
  // general, comes after any OrderBy, Skip or Limit.
  if (body.where()) {
    last_op =
        std::make_unique<Filter>(std::move(last_op), body.where()->expression_);
  }
  return last_op;
}

}  // namespace

namespace impl {

Expression *ExtractFilters(const std::unordered_set<Symbol> &bound_symbols,
                           Filters &filters, AstTreeStorage &storage) {
  Expression *filter_expr = nullptr;
  for (auto filters_it = filters.begin(); filters_it != filters.end();) {
    if (HasBoundFilterSymbols(bound_symbols, *filters_it)) {
      filter_expr = impl::BoolJoin<AndOperator>(storage, filter_expr,
                                                filters_it->expression);
      filters_it = filters.erase(filters_it);
    } else {
      filters_it++;
    }
  }
  return filter_expr;
}

std::unique_ptr<LogicalOperator> GenFilters(
    std::unique_ptr<LogicalOperator> last_op,
    const std::unordered_set<Symbol> &bound_symbols, Filters &filters,
    AstTreeStorage &storage) {
  auto *filter_expr = ExtractFilters(bound_symbols, filters, storage);
  if (filter_expr) {
    last_op = std::make_unique<Filter>(std::move(last_op), filter_expr);
  }
  return last_op;
}

std::unique_ptr<LogicalOperator> GenNamedPaths(
    std::unique_ptr<LogicalOperator> last_op,
    std::unordered_set<Symbol> &bound_symbols,
    std::unordered_map<Symbol, std::vector<Symbol>> &named_paths) {
  auto all_are_bound = [&bound_symbols](const std::vector<Symbol> &syms) {
    for (const auto &sym : syms)
      if (bound_symbols.find(sym) == bound_symbols.end()) return false;
    return true;
  };
  for (auto named_path_it = named_paths.begin();
       named_path_it != named_paths.end();) {
    if (all_are_bound(named_path_it->second)) {
      last_op = std::make_unique<ConstructNamedPath>(
          std::move(last_op), named_path_it->first,
          std::move(named_path_it->second));
      bound_symbols.insert(named_path_it->first);
      named_path_it = named_paths.erase(named_path_it);
    } else {
      ++named_path_it;
    }
  }

  return last_op;
}

std::unique_ptr<LogicalOperator> GenReturn(
    Return &ret, std::unique_ptr<LogicalOperator> input_op,
    SymbolTable &symbol_table, bool is_write,
    const std::unordered_set<Symbol> &bound_symbols, AstTreeStorage &storage) {
  // Similar to WITH clause, but we want to accumulate when the query writes to
  // the database. This way we handle the case when we want to return
  // expressions with the latest updated results. For example, `MATCH (n) -- ()
  // SET n.prop = n.prop + 1 RETURN n.prop`. If we match same `n` multiple 'k'
  // times, we want to return 'k' results where the property value is the same,
  // final result of 'k' increments.
  bool accumulate = is_write;
  bool advance_command = false;
  ReturnBodyContext body(ret.body_, symbol_table, bound_symbols, storage);
  return GenReturnBody(std::move(input_op), advance_command, body, accumulate);
}

std::unique_ptr<LogicalOperator> GenCreateForPattern(
    Pattern &pattern, std::unique_ptr<LogicalOperator> input_op,
    const SymbolTable &symbol_table,
    std::unordered_set<Symbol> &bound_symbols) {
  auto base = [&](NodeAtom *node) -> std::unique_ptr<LogicalOperator> {
    if (bound_symbols.insert(symbol_table.at(*node->identifier_)).second)
      return std::make_unique<CreateNode>(std::move(input_op), node, false);
    else
      return std::move(input_op);
  };

  auto collect = [&](std::unique_ptr<LogicalOperator> last_op,
                     NodeAtom *prev_node, EdgeAtom *edge, NodeAtom *node) {
    // Store the symbol from the first node as the input to CreateExpand.
    const auto &input_symbol = symbol_table.at(*prev_node->identifier_);
    // If the expand node was already bound, then we need to indicate this,
    // so that CreateExpand only creates an edge.
    bool node_existing = false;
    if (!bound_symbols.insert(symbol_table.at(*node->identifier_)).second) {
      node_existing = true;
    }
    if (!bound_symbols.insert(symbol_table.at(*edge->identifier_)).second) {
      LOG(FATAL) << "Symbols used for created edges cannot be redeclared.";
    }
    return std::make_unique<CreateExpand>(node, edge, std::move(last_op),
                                          input_symbol, node_existing);
  };

  auto last_op =
      ReducePattern<std::unique_ptr<LogicalOperator>>(pattern, base, collect);

  // If the pattern is named, append the path constructing logical operator.
  if (pattern.identifier_->user_declared_) {
    std::vector<Symbol> path_elements;
    for (const PatternAtom *atom : pattern.atoms_)
      path_elements.emplace_back(symbol_table.at(*atom->identifier_));
    last_op = std::make_unique<ConstructNamedPath>(
        std::move(last_op), symbol_table.at(*pattern.identifier_),
        path_elements);
  }

  return last_op;
}

// Generate an operator for a clause which writes to the database. Ownership of
// input_op is transferred to the newly created operator. If the clause isn't
// handled, returns nullptr and input_op is left as is.
std::unique_ptr<LogicalOperator> HandleWriteClause(
    Clause *clause, std::unique_ptr<LogicalOperator> &input_op,
    const SymbolTable &symbol_table,
    std::unordered_set<Symbol> &bound_symbols) {
  if (auto *create = dynamic_cast<Create *>(clause)) {
    return GenCreate(*create, std::move(input_op), symbol_table, bound_symbols);
  } else if (auto *del = dynamic_cast<query::Delete *>(clause)) {
    return std::make_unique<plan::Delete>(std::move(input_op),
                                          del->expressions_, del->detach_);
  } else if (auto *set = dynamic_cast<query::SetProperty *>(clause)) {
    return std::make_unique<plan::SetProperty>(
        std::move(input_op), set->property_lookup_, set->expression_);
  } else if (auto *set = dynamic_cast<query::SetProperties *>(clause)) {
    auto op = set->update_ ? plan::SetProperties::Op::UPDATE
                           : plan::SetProperties::Op::REPLACE;
    const auto &input_symbol = symbol_table.at(*set->identifier_);
    return std::make_unique<plan::SetProperties>(
        std::move(input_op), input_symbol, set->expression_, op);
  } else if (auto *set = dynamic_cast<query::SetLabels *>(clause)) {
    const auto &input_symbol = symbol_table.at(*set->identifier_);
    return std::make_unique<plan::SetLabels>(std::move(input_op), input_symbol,
                                             set->labels_);
  } else if (auto *rem = dynamic_cast<query::RemoveProperty *>(clause)) {
    return std::make_unique<plan::RemoveProperty>(std::move(input_op),
                                                  rem->property_lookup_);
  } else if (auto *rem = dynamic_cast<query::RemoveLabels *>(clause)) {
    const auto &input_symbol = symbol_table.at(*rem->identifier_);
    return std::make_unique<plan::RemoveLabels>(std::move(input_op),
                                                input_symbol, rem->labels_);
  }
  return nullptr;
}

std::unique_ptr<LogicalOperator> GenWith(
    With &with, std::unique_ptr<LogicalOperator> input_op,
    SymbolTable &symbol_table, bool is_write,
    std::unordered_set<Symbol> &bound_symbols, AstTreeStorage &storage) {
  // WITH clause is Accumulate/Aggregate (advance_command) + Produce and
  // optional Filter. In case of update and aggregation, we want to accumulate
  // first, so that when aggregating, we get the latest results. Similar to
  // RETURN clause.
  bool accumulate = is_write;
  // No need to advance the command if we only performed reads.
  bool advance_command = is_write;
  ReturnBodyContext body(with.body_, symbol_table, bound_symbols, storage,
                         with.where_);
  auto last_op =
      GenReturnBody(std::move(input_op), advance_command, body, accumulate);
  // Reset bound symbols, so that only those in WITH are exposed.
  bound_symbols.clear();
  for (const auto &symbol : body.output_symbols()) {
    bound_symbols.insert(symbol);
  }
  return last_op;
}

std::unique_ptr<LogicalOperator> GenUnion(
    CypherUnion &cypher_union, std::shared_ptr<LogicalOperator> left_op,
    std::shared_ptr<LogicalOperator> right_op, SymbolTable &symbol_table) {
  return std::make_unique<Union>(left_op, right_op, cypher_union.union_symbols_,
                                 left_op->OutputSymbols(symbol_table),
                                 right_op->OutputSymbols(symbol_table));
}

}  // namespace impl

}  // namespace query::plan
