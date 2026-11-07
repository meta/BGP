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

// Read by jemalloc at init, referenced by nothing else: used+retain keep
// --gc-sections from discarding it. MALLOC_CONF still overrides.
extern "C" {
__attribute__((used, retain)) const char* malloc_conf =
    "background_thread:true,"
    "metadata_thp:auto,"
    "dirty_decay_ms:1000,"
    "muzzy_decay_ms:0";
}
