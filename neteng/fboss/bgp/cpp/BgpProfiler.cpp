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

#include "neteng/fboss/bgp/cpp/BgpProfiler.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"

#include <fb303/ServiceData.h>
#include <folly/FileUtil.h>
#include <folly/json/json.h>
#include <folly/logging/xlog.h>

DEFINE_bool(
    bgp_coro_profiler_enabled,
    false,
    "Enable BGP coroutine profiler (disabled by default, enable via gflag or Thrift)");

DEFINE_bool(
    bgp_coro_profiler_export_ods,
    false,
    "Export profiler stats to ODS");

namespace facebook::bgp {

namespace {
/*
 * Histogram buckets: 1ms resolution, up to 10s
 */
constexpr int64_t kBucketSizeUs = 1000; // 1ms in microseconds
constexpr int64_t kMinUs = 0;
constexpr int64_t kMaxUs = 10000000; // 10s in microseconds

/*
 * Thread-local storage for lock-free profiling
 */
thread_local ProfilerThread currentThread = ProfilerThread::RIB;

} // namespace

CoroStat::ThreadData::ThreadData() : histogram(kBucketSizeUs, kMinUs, kMaxUs) {}

CoroStat::CoroStat() {}

folly::Histogram<int64_t> CoroStat::getMergedHistogram() const {
  folly::Histogram<int64_t> merged = ribThreadData.histogram;
  merged.merge(peerMgrThreadData.histogram);
  return merged;
}

int64_t CoroStat::getMergedMaxUs() const {
  return std::max(ribThreadData.maxUs, peerMgrThreadData.maxUs);
}

int64_t CoroStat::getMergedTotalUs() const {
  return ribThreadData.totalUs + peerMgrThreadData.totalUs;
}

BgpProfiler* BgpProfiler::getInstance() {
  static BgpProfiler* instance = []() {
    auto* p = new BgpProfiler();
    p->enabled_.store(
        FLAGS_bgp_coro_profiler_enabled, std::memory_order_relaxed);
    return p;
  }();
  return instance;
}

void BgpProfiler::setEnabled(bool enabled) {
  enabled_.store(enabled, std::memory_order_relaxed);
  XLOGF(INFO, "BgpProfiler {}", enabled ? "enabled" : "disabled");
}

void BgpProfiler::setFilterRegex(const std::string& regexStr) {
  if (regexStr.empty()) {
    hasFilter_.store(false, std::memory_order_relaxed);
    {
      auto lockedRegex = filterRegex_.wlock();
      *lockedRegex = nullptr;
    }
    {
      auto lockedMatched = matchedNames_.wlock();
      lockedMatched->clear();
    }
    {
      auto lockedChecked = checkedNames_.wlock();
      lockedChecked->clear();
    }
    XLOG(INFO, "BgpProfiler filter cleared");
  } else {
    auto regex = std::make_unique<re2::RE2>(regexStr);
    if (!regex->ok()) {
      XLOGF(ERR, "Invalid regex '{}': {}", regexStr, regex->error());
      return;
    }

    /*
     * Pre-compute matches for all known function names.
     * This eliminates regex overhead in the hot path for known functions.
     */
    folly::F14FastSet<std::string> newMatched;
    folly::F14FastSet<std::string> newChecked;
    {
      auto lockedStats = stats_.rlock();
      for (const auto& [name, _] : *lockedStats) {
        newChecked.insert(name);
        if (re2::RE2::FullMatch(name, *regex)) {
          newMatched.insert(name);
        }
      }
    }

    {
      auto lockedMatched = matchedNames_.wlock();
      *lockedMatched = std::move(newMatched);
    }
    {
      auto lockedChecked = checkedNames_.wlock();
      *lockedChecked = std::move(newChecked);
    }
    {
      auto lockedRegex = filterRegex_.wlock();
      *lockedRegex = std::move(regex);
    }
    hasFilter_.store(true, std::memory_order_relaxed);
    XLOGF(INFO, "BgpProfiler filter set to: {}", regexStr);
  }
}

bool BgpProfiler::matchesFilter(const std::string& name) {
  /*
   * Fast path: no filter means everything matches
   */
  if (!hasFilter_.load(std::memory_order_relaxed)) {
    return true;
  }

  /*
   * Hot path: O(1) hash lookup for pre-computed matches.
   * First check if we've already evaluated this function name.
   */
  {
    auto lockedChecked = checkedNames_.rlock();
    if (lockedChecked->contains(name)) {
      auto lockedMatched = matchedNames_.rlock();
      return lockedMatched->contains(name);
    }
  }

  /*
   * Cold path: new function name seen after filter was set.
   * Run regex once and cache the result.
   */
  bool matches = false;
  {
    auto lockedRegex = filterRegex_.rlock();
    if (*lockedRegex) {
      matches = re2::RE2::FullMatch(name, **lockedRegex);
    }
  }

  /*
   * Cache the result for future lookups
   */
  {
    auto lockedChecked = checkedNames_.wlock();
    lockedChecked->insert(name);
  }
  if (matches) {
    auto lockedMatched = matchedNames_.wlock();
    lockedMatched->insert(name);
  }

  return matches;
}

std::shared_ptr<CoroStat> BgpProfiler::getStat(const std::string& name) {
  /*
   * Check if exists first with read lock
   */
  {
    auto lockedStats = stats_.rlock();
    auto it = lockedStats->find(name);
    if (it != lockedStats->end()) {
      return it->second;
    }
  }

  /*
   * Upgrade to write lock to insert
   */
  auto lockedStats = stats_.wlock();
  auto [it, inserted] = lockedStats->try_emplace(name, nullptr);
  if (inserted) {
    it->second = std::make_shared<CoroStat>();
  }
  return it->second;
}

void BgpProfiler::recordFinish(
    const std::string& name,
    std::chrono::nanoseconds duration) {
  auto stat = getStat(name);
  stat->count.fetch_add(1, std::memory_order_relaxed);

  int64_t us =
      std::chrono::duration_cast<std::chrono::microseconds>(duration).count();

  // Lock-free: each thread writes to its own histogram and max
  auto& data = stat->getThreadData(currentThread);
  data.histogram.addValue(us);
  if (us > data.maxUs) {
    data.maxUs = us;
  }
  data.totalUs += us;
}

void BgpProfiler::recordError(
    const std::string& name,
    std::chrono::nanoseconds duration) {
  /*
   * Treat error same as finish for timing purposes
   */
  recordFinish(name, duration);
}

void BgpProfiler::setCurrentThread(ProfilerThread thread) {
  currentThread = thread;
  XLOGF(
      INFO,
      "BgpProfiler thread set to: {}",
      thread == ProfilerThread::RIB ? "RIB" : "PEER_MANAGER");
}

ProfilerThread BgpProfiler::getCurrentThread() const {
  return currentThread;
}

std::vector<BgpProfiler::StatSummary> BgpProfiler::getStats() const {
  std::vector<StatSummary> result;
  auto lockedStats = stats_.rlock();
  result.reserve(lockedStats->size());

  for (const auto& [name, stat] : *lockedStats) {
    // Lazy merge: only merge histograms when reading stats
    auto merged = stat->getMergedHistogram();
    result.push_back(
        StatSummary{
            .name = name,
            .count = stat->count.load(std::memory_order_relaxed),
            .p50Ms =
                static_cast<int64_t>(merged.getPercentileEstimate(0.5) / 1000),
            .p90Ms =
                static_cast<int64_t>(merged.getPercentileEstimate(0.9) / 1000),
            .p99Ms =
                static_cast<int64_t>(merged.getPercentileEstimate(0.99) / 1000),
            .maxMs = stat->getMergedMaxUs() / 1000,
            .totalMs = stat->getMergedTotalUs() / 1000,
        });
  }
  return result;
}

void BgpProfiler::clearStats() {
  auto lockedStats = stats_.wlock();
  lockedStats->clear();
  XLOG(INFO, "BgpProfiler stats cleared");
}

void BgpProfiler::dumpStatsToFile(const std::string& path) const {
  folly::dynamic json = folly::dynamic::object;
  auto lockedStats = stats_.rlock();

  for (const auto& [name, stat] : *lockedStats) {
    auto merged = stat->getMergedHistogram();
    folly::dynamic statObj = folly::dynamic::object;
    statObj["count"] = stat->count.load();
    statObj["p50_us"] = merged.getPercentileEstimate(0.5);
    statObj["p90_us"] = merged.getPercentileEstimate(0.9);
    statObj["p99_us"] = merged.getPercentileEstimate(0.99);
    statObj["max_us"] = stat->getMergedMaxUs();
    statObj["total_us"] = stat->getMergedTotalUs();

    json[name] = statObj;
  }

  std::string content = folly::toJson(json);
  folly::writeFile(content, path.c_str());
  XLOGF(INFO, "BgpProfiler stats dumped to {}", path);
}

void BgpProfiler::writeToFb303() {
  auto stats = getStats();
  for (const auto& s : stats) {
    /*
     * Sanitize name: "AdjRib::processPeerUpdate" -> "AdjRib.processPeerUpdate"
     * ODS keys cannot contain ::
     */
    const auto& name = s.name;
    std::string key;
    key.reserve(name.size());
    for (size_t i = 0; i < name.size(); ++i) {
      if (i + 1 < name.size() && name[i] == ':' && name[i + 1] == ':') {
        key.push_back('.');
        ++i;
      } else {
        key.push_back(name[i]);
      }
    }

    fb303::fbData->setCounter(
        fmt::format("{}.profiler.{}.count", kBgpcppTag, key), s.count);
    fb303::fbData->setCounter(
        fmt::format("{}.profiler.{}.p50_ms", kBgpcppTag, key), s.p50Ms);
    fb303::fbData->setCounter(
        fmt::format("{}.profiler.{}.p90_ms", kBgpcppTag, key), s.p90Ms);
    fb303::fbData->setCounter(
        fmt::format("{}.profiler.{}.p99_ms", kBgpcppTag, key), s.p99Ms);
    fb303::fbData->setCounter(
        fmt::format("{}.profiler.{}.max_ms", kBgpcppTag, key), s.maxMs);
    fb303::fbData->setCounter(
        fmt::format("{}.profiler.{}.total_ms", kBgpcppTag, key), s.totalMs);
  }
}

/*
 * ScopedProfile implementation
 */
ScopedProfile::ScopedProfile(const std::string& name) : name_(name) {
  auto* profiler = BgpProfiler::getInstance();

  if (!profiler->isEnabled()) [[likely]] {
    return;
  }

  if (profiler->hasFilter() && !profiler->matchesFilter(name_)) {
    return;
  }

  shouldRecord_ = true;
  start_ = std::chrono::steady_clock::now();
}

ScopedProfile::~ScopedProfile() {
  if (shouldRecord_) {
    try {
      auto end = std::chrono::steady_clock::now();
      BgpProfiler::getInstance()->recordFinish(name_, end - start_);
    } catch (...) {
      /*
       * Swallow exceptions to prevent std::terminate during stack unwinding.
       * Losing profiler data is acceptable; crashing is not.
       */
    }
  }
}

} // namespace facebook::bgp
