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

#include <fmt/format.h>
#include <folly/logging/xlog.h>

#include "neteng/fboss/bgp/cpp/adjrib/AdjRib.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibGroup.h"
#include "neteng/fboss/bgp/cpp/adjrib/UpdateGroupManager.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

namespace facebook::bgp {

std::shared_ptr<AdjRibOutGroup> UpdateGroupManager::findOrCreateGroup(
    const UpdateGroupKey& key,
    const ShadowRibView& shadowRib) {
  auto& groups = updateGroups_[key.vrfId];
  auto it = groups.find(key);
  if (it != groups.end()) {
    return it->second;
  }

  auto groupId = nextGroupId_++;
  auto groupName = UpdateGroupKey::toString(key);
  auto group = std::make_shared<AdjRibOutGroup>(
      evb_,
      key.vrfId,
      groupName,
      groupId,
      true /* enableUpdateGroup */,
      key,
      shadowRib,
      policyManager_ /* policyManager */,
      updateGroupConfig_);
  groups[key] = group;

  BgpStats::incrNumUpdateGroups();

  XLOGF(INFO, "Update group: {} is created", group->getGroupDescriptor());

  return group;
}

std::shared_ptr<AdjRibOutGroup> UpdateGroupManager::getGroup(
    const UpdateGroupKey& key) const {
  const auto& groups = getAllGroups(key.vrfId);
  auto it = groups.find(key);
  if (it != groups.end()) {
    return it->second;
  }
  return nullptr;
}

uint64_t UpdateGroupManager::getGroupCount(VrfId vrfId) const {
  return getAllGroups(vrfId).size();
}

bool UpdateGroupManager::hasGroup(const UpdateGroupKey& key) const {
  return getAllGroups(key.vrfId).contains(key);
}

const UpdateGroupManager::UpdateGroupMap& UpdateGroupManager::getAllGroups(
    VrfId vrfId) const noexcept {
  static const UpdateGroupMap kNoGroups;
  auto it = updateGroups_.find(vrfId);
  return it != updateGroups_.end() ? it->second : kNoGroups;
}

folly::coro::Task<void> UpdateGroupManager::maybeDestroyUpdateGroups(
    const folly::F14FastSet<std::shared_ptr<AdjRibOutGroup>>& groups) {
  /*
   * Erase every empty group from the map; the collected shared_ptrs keep them
   * alive so their async scopes can be drained afterwards.
   */
  std::vector<std::shared_ptr<AdjRibOutGroup>> removedGroups;
  for (const auto& group : groups) {
    if (!group || group->getMemberCount() != 0) {
      continue;
    }
    auto vrfIt = updateGroups_.find(group->getVrfId());
    if (vrfIt == updateGroups_.end()) {
      continue;
    }
    auto& vrfGroups = vrfIt->second;
    auto it = vrfGroups.find(group->getGroupKey());
    if (it == vrfGroups.end() || it->second != group) {
      continue;
    }

    /*
     * Technically, drainAsyncScope() will request cancellation
     * on all tasks scheduled in the group's asyncScope. But since we
     * are co_awaiting in a loop, suppose we are on group i, group i + 1
     * still has an opportunity to run the RIB dump before its own
     * drainAsyncScope call. Hence it's important to cancel it above
     * the co_await.
     */
    group->cancelRibDump();
    group->resetChangeListConsumeTimer();
    group->deactivateChangeListConsumer();
    group->resetChangeListConsumer();
    group->clearPackingList();
    group->clearLastSeenRibVersion();

    vrfGroups.erase(it);
    BgpStats::decrNumUpdateGroups();
    removedGroups.push_back(group);
  }

  /*
   * Drain each removed group before it is destroyed, so pending coroutines
   * finish off the EventBase rather than blocking the destructor.
   */
  for (const auto& group : removedGroups) {
    co_await group->drainAsyncScope();
  }
}

void UpdateGroupManager::rekeyGroup(
    const std::shared_ptr<AdjRibOutGroup>& group,
    const UpdateGroupKey& newKey) {
  auto oldKey = group->getGroupKey();

  if (oldKey == newKey) {
    XLOGF(
        DBG1,
        "Group {}: rekeyGroup no-op, key unchanged ({})",
        group->getGroupDescriptor(),
        UpdateGroupKey::toString(oldKey));
    return;
  }

  if (newKey.vrfId != group->getVrfId()) {
    XLOGF(
        ERR,
        "Group {}: rekeyGroup rejected new key {} from another VRF",
        group->getGroupDescriptor(),
        UpdateGroupKey::toString(newKey));
    return;
  }

  group->setGroupKey(newKey);

  auto& groups = updateGroups_[group->getVrfId()];
  auto [it, inserted] = groups.try_emplace(newKey, group);
  if (!inserted) {
    XLOGF(
        ERR,
        "Group {}: rekeyGroup found existing group {} at new key {}, overwriting",
        group->getGroupDescriptor(),
        it->second->getGroupDescriptor(),
        UpdateGroupKey::toString(newKey));
    it->second = group;
  }

  groups.erase(oldKey);

  XLOGF(
      INFO,
      "Group {}: Group key changed from {} to {}",
      group->getGroupDescriptor(),
      UpdateGroupKey::toString(oldKey),
      UpdateGroupKey::toString(newKey));
}

void UpdateGroupManager::setUpdateGroupState(
    const UpdateGroupKey& key,
    UpdateGroupState state) {
  auto group = getGroup(key);
  if (!group) {
    return;
  }

  // Suppress unused parameter warning until implementation
  (void)state;

  /*
   * TODO: the group state set will be implemented later
   */
}

void UpdateGroupManager::triggerInitialDumpsForUninitializedGroups(
    VrfId vrfId) noexcept {
  /*
   * Walk all update groups and trigger initial dump for those in UNINITIALIZED
   * state. Called when BGP initialization completes
   * (markRibInitialAnnouncementDone).
   */
  for (const auto& [key, group] : getAllGroups(vrfId)) {
    if (group->getState() == UpdateGroupState::UNINITIALIZED) {
      // Only trigger if group has members
      if (group->getMemberCount() > 0) {
        group->scheduleInitialDump();
        XLOGF(
            INFO,
            "Triggered initial dump for update group: {}",
            group->getGroupDescriptor());
      } else {
        XLOGF(
            WARN,
            "Skipping initial dump for empty update group: {}",
            group->getGroupDescriptor());
      }
    }
  }
}

void UpdateGroupManager::testOnlySetDeferDrjAcceptance(
    VrfId vrfId,
    const folly::IPAddress& peerAddr,
    bool defer) noexcept {
  for (const auto& [_, group] : getAllGroups(vrfId)) {
    group->testOnlySetDeferDrjAcceptance(peerAddr, defer);
  }
}

} // namespace facebook::bgp
