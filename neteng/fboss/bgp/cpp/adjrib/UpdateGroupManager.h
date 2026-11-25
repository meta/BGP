/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <memory>
#include <utility>
#include <vector>

#include <folly/IPAddress.h>
#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/coro/Task.h>
#include <folly/io/async/EventBase.h>

#include "neteng/fboss/bgp/cpp/adjrib/AdjRibStructs.h"
#include "neteng/fboss/bgp/cpp/adjrib/ShadowRibTypes.h"
#include "neteng/fboss/bgp/cpp/config/ConfigStructs.h"

namespace facebook::bgp {

class AdjRibOutGroup;
class PolicyManager;

/**
 * UpdateGroupManager manages the lifecycle of all update-groups.
 * It provides group lookup, creation, and deletion functionality.
 *
 * Peers with identical UpdateGroupKey are assigned to the same update group
 * to optimize memory and CPU by sharing packing lists and update generation.
 */
class UpdateGroupManager {
 public:
  using UpdateGroupMap = folly::F14NodeMap<
      UpdateGroupKey,
      std::shared_ptr<AdjRibOutGroup>,
      UpdateGroupKeyHash>;

  explicit UpdateGroupManager(
      folly::EventBase& evb,
      const UpdateGroupConfig& updateGroupConfig,
      std::shared_ptr<PolicyManager> policyManager = nullptr)
      : evb_(evb),
        updateGroupConfig_(updateGroupConfig),
        policyManager_(std::move(policyManager)) {}
  ~UpdateGroupManager() = default;

  /*
   * @brief: find or create an update group for the given key.
   * @param: update group key, including the VRF of the group's peers
   * @param: non-owning view of that VRF's shadow RIB entries and max seen RIB
   *   version, handed to a newly created group so a full RIB dump can walk the
   *   table and advance the group to the current version
   * @return: the existing or newly created group for the key.
   */
  std::shared_ptr<AdjRibOutGroup> findOrCreateGroup(
      const UpdateGroupKey& key,
      const ShadowRibView& shadowRib);

  /*
   * @brief: destroy each given update group that has no members.
   * Erases the empty groups from the map, then cooperatively drains their
   * asyncScope_ before destruction to avoid blocking the EventBase thread.
   * Groups that are not empty (or no longer tracked) are skipped.
   * @param: the candidate groups to destroy, from any VRF
   * @note: called during peer termination and group splits/moves to clean up
   *   groups whose members have all left.
   * @return: Task<void> to be co_awaited by the caller.
   */
  folly::coro::Task<void> maybeDestroyUpdateGroups(
      const folly::F14FastSet<std::shared_ptr<AdjRibOutGroup>>& groups);

  /*
   * @brief: set the state of an update group using ENUM.
   * @param: update group key
   * @param: ENUM state
   * @return: none
   */
  void setUpdateGroupState(const UpdateGroupKey& key, UpdateGroupState state);

  /*
   * Get the number of update groups currently managed in the given VRF.
   */
  uint64_t getGroupCount(VrfId vrfId) const;

  /*
   * Check if a group exists for the given key.
   */
  bool hasGroup(const UpdateGroupKey& key) const;

  /*
   * Get an existing group for the given key.
   * Returns nullptr if no group exists.
   */
  std::shared_ptr<AdjRibOutGroup> getGroup(const UpdateGroupKey& key) const;

  /*
   * Get a read-only reference to all update groups of the given VRF.
   * Used by PeerManagerBase to build TUpdateGroupInfo for CLI/thrift.
   */
  const UpdateGroupMap& getAllGroups(VrfId vrfId) const noexcept;

  /*
   * Re-key a group after egress policy re-evaluation. Member keys must already
   * be rebuilt by the caller; this moves the group's entry in the map to the
   * given new key.
   */
  void rekeyGroup(
      const std::shared_ptr<AdjRibOutGroup>& group,
      const UpdateGroupKey& newKey);

  /*
   * Trigger initial dumps for all update groups of the given VRF in
   * UNINITIALIZED state. Called when BGP initialization completes.
   */
  void triggerInitialDumpsForUninitializedGroups(VrfId vrfId) noexcept;

  /*
   * Test-only: defer DRJ acceptance for a specific peer across all
   * update groups of the given VRF. The peer belongs to exactly one group.
   */
  void testOnlySetDeferDrjAcceptance(
      VrfId vrfId,
      const folly::IPAddress& peerAddr,
      bool defer) noexcept;

 private:
  /*
   * EventBase reference for scheduling async operations.
   * Passed to AdjRibOutGroup during construction.
   */
  folly::EventBase& evb_;

  /*
   * Map from VRF to its map from UpdateGroupKey to AdjRibOutGroup.
   * Each unique key within a VRF corresponds to one update group.
   */
  folly::F14NodeMap<VrfId, UpdateGroupMap> updateGroups_;

  /*
   * A monotonically increasing value of groupId to uniquely identify a group.
   */
  uint64_t nextGroupId_{0};

  /*
   * Update group configuration: serialization mode, slow peer detection
   * thresholds, and detachment behavior.
   * Passed from PeerManagerBase and applied to all update groups on creation.
   */
  UpdateGroupConfig updateGroupConfig_;

  /*
   * Shared_ptr to the policyManager instance shared across all
   * peer/update-groups
   */
  std::shared_ptr<PolicyManager> policyManager_{nullptr};
};

} // namespace facebook::bgp
