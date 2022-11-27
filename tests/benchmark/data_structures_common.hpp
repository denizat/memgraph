// Copyright 2022 Memgraph Ltd.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.txt; by using this file, you agree to be bound by the terms of the Business Source
// License, and you may not use this file except in compliance with the Business Source License.
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0, included in the file
// licenses/APL.txt.

#pragma once

#include <map>
#include <set>
#include <vector>

#include "btree_map.hpp"
#include "coordinator/hybrid_logical_clock.hpp"
#include "storage/v3/lexicographically_ordered_vertex.hpp"
#include "storage/v3/mvcc.hpp"
#include "storage/v3/transaction.hpp"
#include "utils/skip_list.hpp"

namespace memgraph::benchmark {

template <typename T>
inline void PrepareData(utils::SkipList<T> &skip_list, const int64_t num_elements) {
  coordinator::Hlc start_timestamp;
  storage::v3::IsolationLevel isolation_level{storage::v3::IsolationLevel::SNAPSHOT_ISOLATION};
  storage::v3::Transaction transaction{start_timestamp, isolation_level};
  auto *delta = storage::v3::CreateDeleteObjectDelta(&transaction);
  for (auto i{0}; i < num_elements; ++i) {
    auto acc = skip_list.access();
    acc.insert({storage::v3::Vertex(delta, std::vector<storage::v3::PropertyValue>{storage::v3::PropertyValue{i}})});
  }
}

template <typename TKey, typename TValue>
inline void PrepareData(std::map<TKey, TValue> &std_map, const int64_t num_elements) {
  coordinator::Hlc start_timestamp;
  storage::v3::IsolationLevel isolation_level{storage::v3::IsolationLevel::SNAPSHOT_ISOLATION};
  storage::v3::Transaction transaction{start_timestamp, isolation_level};
  auto *delta = storage::v3::CreateDeleteObjectDelta(&transaction);
  for (auto i{0}; i < num_elements; ++i) {
    std_map.insert({storage::v3::PrimaryKey{storage::v3::PropertyValue{i}},
                    storage::v3::LexicographicallyOrderedVertex{storage::v3::Vertex{
                        delta, std::vector<storage::v3::PropertyValue>{storage::v3::PropertyValue{i}}}}});
  }
}

template <typename T>
inline void PrepareData(std::set<T> &std_set, const int64_t num_elements) {
  coordinator::Hlc start_timestamp;
  storage::v3::IsolationLevel isolation_level{storage::v3::IsolationLevel::SNAPSHOT_ISOLATION};
  storage::v3::Transaction transaction{start_timestamp, isolation_level};
  auto *delta = storage::v3::CreateDeleteObjectDelta(&transaction);
  for (auto i{0}; i < num_elements; ++i) {
    std_set.insert(storage::v3::LexicographicallyOrderedVertex{
        storage::v3::Vertex{delta, std::vector<storage::v3::PropertyValue>{storage::v3::PropertyValue{i}}}});
  }
}

template <typename TKey, typename TValue>
inline void PrepareData(tlx::btree_map<TKey, TValue> &bpp_tree, const int64_t num_elements) {
  coordinator::Hlc start_timestamp;
  storage::v3::IsolationLevel isolation_level{storage::v3::IsolationLevel::SNAPSHOT_ISOLATION};
  storage::v3::Transaction transaction{start_timestamp, isolation_level};
  auto *delta = storage::v3::CreateDeleteObjectDelta(&transaction);
  for (auto i{0}; i < num_elements; ++i) {
    bpp_tree.insert({storage::v3::PrimaryKey{storage::v3::PropertyValue{i}},
                     storage::v3::LexicographicallyOrderedVertex{storage::v3::Vertex{
                         delta, std::vector<storage::v3::PropertyValue>{storage::v3::PropertyValue{i}}}}});
  }
}
}  // namespace memgraph::benchmark
