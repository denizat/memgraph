// Copyright 2024 Memgraph Ltd.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.txt; by using this file, you agree to be bound by the terms of the Business Source
// License, and you may not use this file except in compliance with the Business Source License.
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0, included in the file
// licenses/APL.txt.

#include "storage/v2/indices/text_index.hpp"
#include "query/db_accessor.hpp"
#include "storage/v2/mgcxx_mock.hpp"
#include "storage/v2/view.hpp"
#include "text_search.hpp"

namespace memgraph::storage {

void TextIndex::UpdateOnAddLabel(LabelId added_label, Vertex *vertex_after_update, const Transaction &tx) const {
  // Add node to this label's text index
}

void TextIndex::UpdateOnRemoveLabel(LabelId removed_label, Vertex *vertex_after_update, const Transaction &tx) const {
  // Remove node from this label's text index
}

void TextIndex::UpdateOnSetProperty(PropertyId property, const PropertyValue &value, Vertex *vertex,
                                    const Transaction &tx) const {
  // Delete this node's document and re-add the it with the new value
}

std::vector<memcxx::text_search::Context *> TextIndex::GetApplicableTextIndices(Vertex *vertex) {
  std::vector<memcxx::text_search::Context *> applicable_text_indices;
  for (const auto &label : vertex->labels) {
    if (label_to_index_.contains(label)) {
      applicable_text_indices.push_back(&index_.at(label_to_index_.at(label)));
    }
  }
  return applicable_text_indices;
}

bool TextIndex::CreateIndex(std::string index_name, LabelId label, memgraph::query::DbAccessor *db) {
  nlohmann::json mappings = {};
  mappings["properties"] = {};
  mappings["properties"]["metadata"] = {{"type", "json"}, {"fast", true}, {"stored", true}, {"text", true}};
  mappings["properties"]["data"] = {{"type", "json"}, {"fast", true}, {"stored", true}, {"text", true}};

  index_.emplace(index_name, memcxx::text_search::create_index(
                                 index_name, memcxx::text_search::IndexConfig{.mappings = mappings.dump()}));
  label_to_index_.emplace(label, index_name);

  bool has_schema = false;
  std::vector<std::pair<PropertyId, std::string>> indexed_properties{};
  for (const auto &v : db->Vertices(View::OLD)) {
    if (!has_schema) [[unlikely]] {
      for (const auto &[prop_id, prop_val] : v.Properties(View::OLD).GetValue()) {
        if (prop_val.IsString()) {
          indexed_properties.emplace_back(std::pair<PropertyId, std::string>{prop_id, db->PropertyToName(prop_id)});
        }
      }
      has_schema = true;
    }

    nlohmann::json document = {};
    nlohmann::json properties = {};
    for (const auto &[prop_id, prop_name] : indexed_properties) {
      properties[prop_name] = v.GetProperty(View::OLD, prop_id).GetValue().ValueString();
    }

    document["data"] = properties;
    document["metadata"] = {};
    document["metadata"]["gid"] = v.Gid().AsInt();
    // TODO add txid
    document["metadata"]["deleted"] = false;
    document["metadata"]["is_node"] = true;

    memcxx::text_search::add(index_.at(index_name), memcxx::text_search::DocumentInput{.data = document.dump()}, false);
  }
  return true;
}

bool TextIndex::DropIndex(std::string index_name) {
  memcxx::text_search::drop_index(index_name);
  index_.erase(index_name);
  std::erase_if(label_to_index_, [index_name](const auto &item) { return item.second == index_name; });
  return true;
}

bool TextIndex::IndexExists(std::string index_name) const { return index_.contains(index_name); }

std::vector<Gid> TextIndex::Search(std::string index_name, std::string search_query) {
  auto input = memcxx::text_search::SearchInput{.search_query = search_query, .return_fields = {"metadata"}};
  // Basic check for search fields in the query (Tantivy syntax delimits them with a `:` to the right)
  if (search_query.find(":") == std::string::npos) {
    input.search_fields = {"data"};
  }

  std::vector<Gid> found_nodes;
  for (const auto &doc : memcxx::text_search::search(index_.at(index_name), input).docs) {
    found_nodes.push_back(storage::Gid::FromString(nlohmann::json::parse(doc.data.data())["metadata"]["gid"].dump()));
  }
  return found_nodes;
}

std::vector<std::string> TextIndex::ListIndices() const {
  std::vector<std::string> ret;
  ret.reserve(index_.size());
  for (const auto &item : index_) {
    ret.push_back(item.first);
  }
  return ret;
}

std::uint64_t TextIndex::ApproximateVertexCount(std::string index_name) const { return 10; }

}  // namespace memgraph::storage
