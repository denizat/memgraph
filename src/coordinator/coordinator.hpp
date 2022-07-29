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

#include "coordinator/hybrid_logical_clock.hpp"
#include "coordinator/shard_map.hpp"
#include "io/simulator/simulator.hpp"
#include "io/time.hpp"
#include "io/transport.hpp"

namespace memgraph::coordinator {

using Address = memgraph::io::Address;
using Io = memgraph::io::Io;
using SimT = memgraph::io::simulator::SimulatorTransport;

struct SplitShardRequest {
  Hlc previous_shard_map_version;
  Label label;
  CompoundKey split_key;
};

struct SplitShardResponse {
  bool success;
};

struct RegisterStorageEngineRequest {
  Address address;
};

struct RegisterStorageEngineResponse {
  bool success;
};

struct DeregisterStorageEngineRequest {
  Address address;
};

struct DeregisterStorageEngineResponse {
  bool success;
};

struct HlcRequest {
  Hlc last_shard_map_version;
};

struct HlcResponse {
  Hlc new_hlc;
  std::optional<ShardMap> fresher_shard_map;
};

class Coordinator {
  ShardMap shard_map_;
  Io<SimT> io_;

  /// This splits the shard
  void Handle(SplitShardRequest &split_shard_request, Address from_addr) {
    if (split_shard_request.previous_shard_map_version != shard_map_.shard_map_version) {
      // TODO reply with failure
    }
  }

  void Handle(RegisterStorageEngineRequest &register_storage_engine_request, Address from_addr) {}

 public:
  void Run() {
    while (!io_.ShouldShutDown()) {
      std::cout << "[Coordinator] Is receiving..." << std::endl;
      auto request_result =
          io_.Receive<SplitShardRequest, RegisterStorageEngineRequest, DeregisterStorageEngineRequest, HlcRequest>();
      if (request_result.HasError()) {
        std::cout << "[Coordinator] Error, continue" << std::endl;
        continue;
      }

      auto request_envelope = request_result.GetValue();
      // TODO std::visit to determine whether to handle shard split, registration etc... (see raft.hpp Run / Handle
      // methods in T0941)
    }
  }
};

}  // namespace memgraph::coordinator
