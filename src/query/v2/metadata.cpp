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

#include "query/v2/metadata.hpp"

#include <algorithm>
#include <compare>
#include <string>
#include <string_view>

namespace memgraph::query::v2 {

namespace {
using namespace std::literals;

constexpr std::string_view GetSeverityLevelString(const SeverityLevel level) {
  switch (level) {
    case SeverityLevel::INFO:
      return "INFO"sv;
    case SeverityLevel::WARNING:
      return "WARNING"sv;
  }
}

constexpr std::string_view GetCodeString(const NotificationCode code) {
  switch (code) {
    case NotificationCode::CREATE_CONSTRAINT:
      return "CreateConstraint"sv;
    case NotificationCode::CREATE_INDEX:
      return "CreateIndex"sv;
    case NotificationCode::CREATE_STREAM:
      return "CreateStream"sv;
    case NotificationCode::CREATE_SCHEMA:
      return "CreateSchema"sv;
    case NotificationCode::CHECK_STREAM:
      return "CheckStream"sv;
    case NotificationCode::CREATE_TRIGGER:
      return "CreateTrigger"sv;
    case NotificationCode::DROP_CONSTRAINT:
      return "DropConstraint"sv;
    case NotificationCode::DROP_REPLICA:
      return "DropReplica"sv;
    case NotificationCode::DROP_INDEX:
      return "DropIndex"sv;
    case NotificationCode::DROP_SCHEMA:
      return "DropSchema"sv;
    case NotificationCode::DROP_STREAM:
      return "DropStream"sv;
    case NotificationCode::DROP_TRIGGER:
      return "DropTrigger"sv;
    case NotificationCode::EXISTANT_CONSTRAINT:
      return "ConstraintAlreadyExists"sv;
    case NotificationCode::EXISTANT_INDEX:
      return "IndexAlreadyExists"sv;
    case NotificationCode::LOAD_CSV_TIP:
      return "LoadCSVTip"sv;
    case NotificationCode::NONEXISTANT_INDEX:
      return "IndexDoesNotExist"sv;
    case NotificationCode::NONEXISTANT_CONSTRAINT:
      return "ConstraintDoesNotExist"sv;
    case NotificationCode::REGISTER_REPLICA:
      return "RegisterReplica"sv;
    case NotificationCode::REPLICA_PORT_WARNING:
      return "ReplicaPortWarning"sv;
    case NotificationCode::SET_REPLICA:
      return "SetReplica"sv;
    case NotificationCode::SHOW_SCHEMA:
      return "ShowSchema"sv;
    case NotificationCode::SHOW_SCHEMAS:
      return "ShowSchemas"sv;
    case NotificationCode::START_STREAM:
      return "StartStream"sv;
    case NotificationCode::START_ALL_STREAMS:
      return "StartAllStreams"sv;
    case NotificationCode::STOP_STREAM:
      return "StopStream"sv;
    case NotificationCode::STOP_ALL_STREAMS:
      return "StopAllStreams"sv;
  }
}
}  // namespace

Notification::Notification(SeverityLevel level) : level{level} {};

Notification::Notification(SeverityLevel level, NotificationCode code, std::string title, std::string description)
    : level{level}, code{code}, title(std::move(title)), description(std::move(description)){};

Notification::Notification(SeverityLevel level, NotificationCode code, std::string title)
    : level{level}, code{code}, title(std::move(title)){};

std::map<std::string, TypedValue> Notification::ConvertToMap() const {
  return std::map<std::string, TypedValue>{{"severity", TypedValue(GetSeverityLevelString(level))},
                                           {"code", TypedValue(GetCodeString(code))},
                                           {"title", TypedValue(title)},
                                           {"description", TypedValue(description)}};
}

std::string ExecutionStatsKeyToString(const ExecutionStats::Key key) {
  switch (key) {
    case ExecutionStats::Key::CREATED_NODES:
      return std::string("nodes-created");
    case ExecutionStats::Key::DELETED_NODES:
      return std::string("nodes-deleted");
    case ExecutionStats::Key::CREATED_EDGES:
      return std::string("relationships-created");
    case ExecutionStats::Key::DELETED_EDGES:
      return std::string("relationships-deleted");
    case ExecutionStats::Key::CREATED_LABELS:
      return std::string("labels-added");
    case ExecutionStats::Key::DELETED_LABELS:
      return std::string("labels-removed");
    case ExecutionStats::Key::UPDATED_PROPERTIES:
      return std::string("properties-set");
  }
}

}  // namespace memgraph::query::v2
