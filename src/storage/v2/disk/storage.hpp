// Copyright 2023 Memgraph Ltd.
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

#include "storage/v2/disk/rocksdb_storage.hpp"
#include "storage/v2/storage.hpp"

/// ROCKSDB
#include <rocksdb/db.h>

namespace memgraph::storage {

class DiskStorage final : public Storage {
 public:
  /// @throw std::system_error
  /// @throw std::bad_alloc
  explicit DiskStorage(Config config = Config());

  ~DiskStorage() override;

  class DiskAccessor final : public Storage::Accessor {
   private:
    friend class DiskStorage;

    explicit DiskAccessor(DiskStorage *storage, IsolationLevel isolation_level, StorageMode storage_mode);

   public:
    DiskAccessor(const DiskAccessor &) = delete;
    DiskAccessor &operator=(const DiskAccessor &) = delete;
    DiskAccessor &operator=(DiskAccessor &&other) = delete;

    // NOTE: After the accessor is moved, all objects derived from it (accessors
    // and iterators) are *invalid*. You have to get all derived objects again.
    DiskAccessor(DiskAccessor &&other) noexcept;

    ~DiskAccessor() override;

    VertexAccessor CreateVertex() override;

    /// Checks whether the vertex with the given `gid` exists in the vertices_. If it does, it returns a
    /// VertexAccessor to it. If it doesn't, it fetches vertex from the RocksDB. If it doesn't exist in the RocksDB
    /// either, it returns nullptr. If the vertex is fetched from the RocksDB, it is inserted into the vertices_ and
    /// lru_vertices_. Check whether the vertex is in the memory cache (vertices_) is done in O(logK) where K is the
    /// size of the cache.
    std::optional<VertexAccessor> FindVertex(Gid gid, View view) override;

    /// Utility method to load all vertices from the underlying KV storage.
    VerticesIterable Vertices(View view) override;

    /// Utility method to load all vertices from the underlying KV storage with label `label`.
    VerticesIterable Vertices(LabelId label, View view) override;

    VerticesIterable Vertices(LabelId label, PropertyId property, View view) override;

    VerticesIterable Vertices(LabelId label, PropertyId property, const PropertyValue &value, View view) override;

    VerticesIterable Vertices(LabelId label, PropertyId property,
                              const std::optional<utils::Bound<PropertyValue>> &lower_bound,
                              const std::optional<utils::Bound<PropertyValue>> &upper_bound, View view) override;

    int64_t ApproximateVertexCount() const override;

    int64_t ApproximateVertexCount(LabelId label) const override {
      throw utils::NotYetImplemented("ApproximateVertexCount(label) is not implemented for DiskStorage.");
    }

    int64_t ApproximateVertexCount(LabelId label, PropertyId property) const override {
      throw utils::NotYetImplemented("ApproximateVertexCount(label, property) is not implemented for DiskStorage.");
    }

    int64_t ApproximateVertexCount(LabelId label, PropertyId property, const PropertyValue &value) const override {
      throw utils::NotYetImplemented(
          "ApproximateVertexCount(label, property, value) is not implemented for DiskStorage.");
    }

    int64_t ApproximateVertexCount(LabelId label, PropertyId property,
                                   const std::optional<utils::Bound<PropertyValue>> &lower,
                                   const std::optional<utils::Bound<PropertyValue>> &upper) const override {
      throw utils::NotYetImplemented(
          "ApproximateVertexCount(label, property, lower, upper) is not implemented for DiskStorage.");
    }

    std::optional<storage::IndexStats> GetIndexStats(const storage::LabelId &label,
                                                     const storage::PropertyId &property) const override {
      throw utils::NotYetImplemented("GetIndexStats() is not implemented for DiskStorage.");
    }

    std::vector<std::pair<LabelId, PropertyId>> ClearIndexStats() override {
      throw utils::NotYetImplemented("ClearIndexStats() is not implemented for DiskStorage.");
    }

    std::vector<std::pair<LabelId, PropertyId>> DeleteIndexStatsForLabels(
        const std::span<std::string> labels) override {
      throw utils::NotYetImplemented("DeleteIndexStatsForLabels(labels) is not implemented for DiskStorage.");
    }

    void SetIndexStats(const storage::LabelId &label, const storage::PropertyId &property,
                       const IndexStats &stats) override {
      throw utils::NotYetImplemented("SetIndexStats(stats) is not implemented for DiskStorage.");
    }

    /// Deletes vertex only from the cache if it was created in the same transaction.
    /// If the vertex was fetched from the RocksDB, it is deleted from the RocksDB.
    /// It is impossible that the object isn't in the cache because of generated query plan.
    Result<std::optional<VertexAccessor>> DeleteVertex(VertexAccessor *vertex) override;

    Result<std::optional<std::pair<VertexAccessor, std::vector<EdgeAccessor>>>> DetachDeleteVertex(
        VertexAccessor *vertex) override;

    void PrefetchInEdges(const VertexAccessor &vertex_acc) override;

    void PrefetchOutEdges(const VertexAccessor &vertex_acc) override;

    Result<EdgeAccessor> CreateEdge(VertexAccessor *from, VertexAccessor *to, EdgeTypeId edge_type) override;

    Result<std::optional<EdgeAccessor>> DeleteEdge(EdgeAccessor *edge) override;

    bool LabelIndexExists(LabelId label) const override { throw utils::NotYetImplemented("LabelIndexExists()"); }

    bool LabelPropertyIndexExists(LabelId label, PropertyId property) const override {
      throw utils::NotYetImplemented("LabelPropertyIndexExists() is not implemented for DiskStorage.");
    }

    IndicesInfo ListAllIndices() const override {
      throw utils::NotYetImplemented("ListAllIndices() is not implemented for DiskStorage.");
    }

    ConstraintsInfo ListAllConstraints() const override {
      throw utils::NotYetImplemented("ListAllConstraints() is not implemented for DiskStorage.");
    }

    utils::BasicResult<StorageDataManipulationError, void> Commit(
        std::optional<uint64_t> desired_commit_timestamp = {}) override;

    /// @throw std::bad_alloc
    /// Currently, it does everything the same as in-memory version.
    void Abort() override;

    /// Currently, it does everything the same as in-memory version.
    void FinalizeTransaction() override;

    /// Deserializes vertex from the string key and stores it into the vertices_ and lru_vertices_.
    /// Properties are deserialized from the value.
    /// The method should be called only when the vertex is not in the cache.
    std::optional<storage::VertexAccessor> DeserializeVertex(const rocksdb::Slice &key, const rocksdb::Slice &value);

    /// Deserializes edge from the string key and stores it into the edges_ cache.
    /// Properties are deserialized from the value.
    /// The method should be called only when the edge is not in the cache.
    std::optional<storage::EdgeAccessor> DeserializeEdge(const rocksdb::Slice &key, const rocksdb::Slice &value);

   private:
    /// TODO(andi): Consolidate this vertex creation methods and find from in-memory version where are they used.
    /// Used for deserialization of vertices and edges from KV store.
    /// @throw std::bad_alloc
    VertexAccessor CreateVertex(storage::Gid gid);

    /// TODO(andi): Consolidate this vertex creation methods and find from in-memory version where are they used.
    VertexAccessor CreateVertex(storage::Gid gid, uint64_t vertex_commit_ts, std::vector<LabelId> label_ids,
                                std::string_view properties);

    void PrefetchEdges(const auto &prefetch_edge_filter);

    /// @throw std::bad_alloc
    /// TODO(andi): Consolidate this vertex creation methods and find from in-memory version where are they used.
    Result<EdgeAccessor> CreateEdge(VertexAccessor *from, VertexAccessor *to, EdgeTypeId edge_type, storage::Gid gid);

    /// TODO(andi): Consolidate this vertex creation methods and find from in-memory version where are they used.
    Result<EdgeAccessor> CreateEdge(VertexAccessor *from, VertexAccessor *to, EdgeTypeId edge_type, storage::Gid gid,
                                    uint64_t edge_commit_ts, std::string_view properties);

    /// Flushes vertices and edges to the disk with the commit timestamp.
    /// At the time of calling, the commit_timestamp_ must already exist.
    /// After this method, the vertex and edge caches are cleared.
    void FlushCache();

    Config::Items config_;
    std::vector<std::string> edges_to_delete_;
    std::vector<std::string> vertices_to_delete_;
  };

  std::unique_ptr<Storage::Accessor> Access(std::optional<IsolationLevel> override_isolation_level) override {
    return std::unique_ptr<DiskAccessor>(
        new DiskAccessor{this, override_isolation_level.value_or(isolation_level_), storage_mode_});
  }

  /// Create an index.
  /// Returns void if the index has been created.
  /// Returns `StorageIndexDefinitionError` if an error occures. Error can be:
  /// * `IndexDefinitionError`: the index already exists.
  /// * `ReplicationError`:  there is at least one SYNC replica that has not confirmed receiving the transaction.
  /// @throw std::bad_alloc
  utils::BasicResult<StorageIndexDefinitionError, void> CreateIndex(
      LabelId label, std::optional<uint64_t> desired_commit_timestamp) override;

  /// Create an index.
  /// Returns void if the index has been created.
  /// Returns `StorageIndexDefinitionError` if an error occures. Error can be:
  /// * `ReplicationError`:  there is at least one SYNC replica that has not confirmed receiving the transaction.
  /// * `IndexDefinitionError`: the index already exists.
  /// @throw std::bad_alloc
  utils::BasicResult<StorageIndexDefinitionError, void> CreateIndex(
      LabelId label, PropertyId property, std::optional<uint64_t> desired_commit_timestamp) override;

  /// Drop an existing index.
  /// Returns void if the index has been dropped.
  /// Returns `StorageIndexDefinitionError` if an error occures. Error can be:
  /// * `ReplicationError`:  there is at least one SYNC replica that has not confirmed receiving the transaction.
  /// * `IndexDefinitionError`: the index does not exist.
  utils::BasicResult<StorageIndexDefinitionError, void> DropIndex(
      LabelId label, std::optional<uint64_t> desired_commit_timestamp) override;

  /// Drop an existing index.
  /// Returns void if the index has been dropped.
  /// Returns `StorageIndexDefinitionError` if an error occures. Error can be:
  /// * `ReplicationError`:  there is at least one SYNC replica that has not confirmed receiving the transaction.
  /// * `IndexDefinitionError`: the index does not exist.
  utils::BasicResult<StorageIndexDefinitionError, void> DropIndex(
      LabelId label, PropertyId property, std::optional<uint64_t> desired_commit_timestamp) override;

  IndicesInfo ListAllIndices() const override;

  /// Returns void if the existence constraint has been created.
  /// Returns `StorageExistenceConstraintDefinitionError` if an error occures. Error can be:
  /// * `ReplicationError`: there is at least one SYNC replica that has not confirmed receiving the transaction.
  /// * `ConstraintViolation`: there is already a vertex existing that would break this new constraint.
  /// * `ConstraintDefinitionError`: the constraint already exists.
  /// @throw std::bad_alloc
  /// @throw std::length_error
  utils::BasicResult<StorageExistenceConstraintDefinitionError, void> CreateExistenceConstraint(
      LabelId label, PropertyId property, std::optional<uint64_t> desired_commit_timestamp) override;

  /// Drop an existing existence constraint.
  /// Returns void if the existence constraint has been dropped.
  /// Returns `StorageExistenceConstraintDroppingError` if an error occures. Error can be:
  /// * `ReplicationError`: there is at least one SYNC replica that has not confirmed receiving the transaction.
  /// * `ConstraintDefinitionError`: the constraint did not exists.
  utils::BasicResult<StorageExistenceConstraintDroppingError, void> DropExistenceConstraint(
      LabelId label, PropertyId property, std::optional<uint64_t> desired_commit_timestamp) override;

  /// Create an unique constraint.
  /// Returns `StorageUniqueConstraintDefinitionError` if an error occures. Error can be:
  /// * `ReplicationError`: there is at least one SYNC replica that has not confirmed receiving the transaction.
  /// * `ConstraintViolation`: there are already vertices violating the constraint.
  /// Returns `UniqueConstraints::CreationStatus` otherwise. Value can be:
  /// * `SUCCESS` if the constraint was successfully created,
  /// * `ALREADY_EXISTS` if the constraint already existed,
  /// * `EMPTY_PROPERTIES` if the property set is empty, or
  /// * `PROPERTIES_SIZE_LIMIT_EXCEEDED` if the property set exceeds the limit of maximum number of properties.
  /// @throw std::bad_alloc
  utils::BasicResult<StorageUniqueConstraintDefinitionError, UniqueConstraints::CreationStatus> CreateUniqueConstraint(
      LabelId label, const std::set<PropertyId> &properties, std::optional<uint64_t> desired_commit_timestamp) override;

  /// Removes an existing unique constraint.
  /// Returns `StorageUniqueConstraintDroppingError` if an error occures. Error can be:
  /// * `ReplicationError`: there is at least one SYNC replica that has not confirmed receiving the transaction.
  /// Returns `UniqueConstraints::DeletionStatus` otherwise. Value can be:
  /// * `SUCCESS` if constraint was successfully removed,
  /// * `NOT_FOUND` if the specified constraint was not found,
  /// * `EMPTY_PROPERTIES` if the property set is empty, or
  /// * `PROPERTIES_SIZE_LIMIT_EXCEEDED` if the property set exceeds the limit of maximum number of properties.
  utils::BasicResult<StorageUniqueConstraintDroppingError, UniqueConstraints::DeletionStatus> DropUniqueConstraint(
      LabelId label, const std::set<PropertyId> &properties, std::optional<uint64_t> desired_commit_timestamp) override;

  ConstraintsInfo ListAllConstraints() const override;

  bool SetReplicaRole(io::network::Endpoint endpoint, const replication::ReplicationServerConfig &config) override;

  bool SetMainReplicationRole() override;

  /// @pre The instance should have a MAIN role
  /// @pre Timeout can only be set for SYNC replication
  utils::BasicResult<RegisterReplicaError, void> RegisterReplica(
      std::string name, io::network::Endpoint endpoint, replication::ReplicationMode replication_mode,
      replication::RegistrationMode registration_mode, const replication::ReplicationClientConfig &config) override;
  /// @pre The instance should have a MAIN role
  bool UnregisterReplica(const std::string &name) override;

  std::optional<replication::ReplicaState> GetReplicaState(std::string_view name) override;

  ReplicationRole GetReplicationRole() const override;

  std::vector<ReplicaInfo> ReplicasInfo() override;

  void FreeMemory() override;

  utils::BasicResult<SetIsolationLevelError> SetIsolationLevel(IsolationLevel isolation_level) override;

  void SetStorageMode(StorageMode storage_mode) override;

  StorageMode GetStorageMode() override;

  utils::BasicResult<CreateSnapshotError> CreateSnapshot(std::optional<bool> is_periodic) override;

  Transaction CreateTransaction(IsolationLevel isolation_level, StorageMode storage_mode) override;

 private:
  /// The force parameter determines the behaviour of the garbage collector.
  /// If it's set to true, it will behave as a global operation, i.e. it can't
  /// be part of a transaction, and no other transaction can be active at the same time.
  /// This allows it to delete immediately vertices without worrying that some other
  /// transaction is possibly using it. If there are active transactions when this method
  /// is called with force set to true, it will fallback to the same method with the force
  /// set to false.
  /// If it's set to false, it will execute in parallel with other transactions, ensuring
  /// that no object in use can be deleted.
  /// @throw std::system_error
  /// @throw std::bad_alloc
  template <bool force>
  void CollectGarbage();

  bool InitializeWalFile();
  void FinalizeWalFile();

  /// Return true in all cases excepted if any sync replicas have not sent confirmation.
  [[nodiscard]] bool AppendToWalDataManipulation(const Transaction &transaction, uint64_t final_commit_timestamp);
  /// Return true in all cases excepted if any sync replicas have not sent confirmation.
  [[nodiscard]] bool AppendToWalDataDefinition(durability::StorageGlobalOperation operation, LabelId label,
                                               const std::set<PropertyId> &properties, uint64_t final_commit_timestamp);

  uint64_t CommitTimestamp(std::optional<uint64_t> desired_commit_timestamp = {});

  void RestoreReplicas();

  bool ShouldStoreAndRestoreReplicas() const;

  Constraints constraints_;
  Indices indices_;
  IsolationLevel isolation_level_;
  StorageMode storage_mode_;

  // TODO: This isn't really a commit log, it doesn't even care if a
  // transaction commited or aborted. We could probably combine this with
  // `timestamp_` in a sensible unit, something like TransactionClock or
  // whatever.
  std::optional<CommitLog> commit_log_;
  utils::Synchronized<std::list<Transaction>, utils::SpinLock> committed_transactions_;
  utils::Scheduler gc_runner_;
  std::mutex gc_lock_;

  // Undo buffers that were unlinked and now are waiting to be freed.
  utils::Synchronized<std::list<std::pair<uint64_t, std::list<Delta>>>, utils::SpinLock> garbage_undo_buffers_;

  // Vertices that are logically deleted but still have to be removed from
  // indices before removing them from the main storage.
  utils::Synchronized<std::list<Gid>, utils::SpinLock> deleted_vertices_;

  // Vertices that are logically deleted and removed from indices and now wait
  // to be removed from the main storage.
  std::list<std::pair<uint64_t, Gid>> garbage_vertices_;

  // Edges that are logically deleted and wait to be removed from the main
  // storage.
  utils::Synchronized<std::list<Gid>, utils::SpinLock> deleted_edges_;

  // class ReplicationServer;
  // std::unique_ptr<ReplicationServer> replication_server_{nullptr};

  // class ReplicationClient;
  // We create ReplicationClient using unique_ptr so we can move
  // newly created client into the vector.
  // We cannot move the client directly because it contains ThreadPool
  // which cannot be moved. Also, the move is necessary because
  // we don't want to create the client directly inside the vector
  // because that would require the lock on the list putting all
  // commits (they iterate list of clients) to halt.
  // This way we can initialize client in main thread which means
  // that we can immediately notify the user if the initialization
  // failed.
  // using ReplicationClientList = utils::Synchronized<std::vector<std::unique_ptr<ReplicationClient>>,
  // utils::SpinLock>; ReplicationClientList replication_clients_;

  // std::atomic<ReplicationRole> replication_role_{ReplicationRole::MAIN};

  std::unique_ptr<RocksDBStorage> kvstore_;
};

}  // namespace memgraph::storage
