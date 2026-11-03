# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# pyre-strict

import importlib.resources
import unittest
from typing import Any

import yaml


"""
Guard test for the EOS CLI-extension grammar in BgpCli.yaml.

BgpCli.yaml is loaded by the EOS CliExtension framework to build the
`show bgpcpp ...` command tree. The token help strings are written as
unquoted plain scalars inside YAML flow mappings ({ keyword: { help: ... } }),
where ',' '[' ']' '{' '}' are structural characters. A stray one of those in
a help string makes the whole file fail to parse, which silently drops the
ENTIRE `show bgpcpp` command tree on the device (leaving only tokens
registered by other extensions, e.g. drain-state). This test proves the file
still parses and exposes the expected tokens so that class of regression is
caught at build time rather than on a lab switch.
"""

# Tokens the `show bgpcpp` grammar must always expose.
_EXPECTED_SHOW_BGPCPP_TOKENS = (
    "nexthopinfo",
    "holdtimers",
    "summary",
    "neighbors",
    "profiler",
    "sort-by",
    "<sortKeys>",
)


class BgpCliYamlTest(unittest.TestCase):
    def _load(self) -> dict[str, Any]:
        text = (
            importlib.resources.files(__package__)
            .joinpath("BgpCli.yaml")
            .read_text(encoding="utf-8")
        )
        # safe_load raises on a malformed flow scalar (the regression this
        # test guards against), failing the test loudly.
        return yaml.safe_load(text)

    def test_yaml_parses_and_exposes_show_bgpcpp_tokens(self) -> None:
        doc = self._load()
        self.assertIsInstance(doc, dict)

        commands = doc["commands"]
        self.assertIn("showBgpCpp", commands)
        self.assertIn("showBgpVersion", commands)

        data = commands["showBgpCpp"]["data"]
        self.assertIsInstance(data, dict)
        for token in _EXPECTED_SHOW_BGPCPP_TOKENS:
            self.assertIn(token, data)

    def test_profiler_token_is_reachable_in_syntax(self) -> None:
        # A token declared only in 'data' is dead: the syntax alternation is
        # what makes 'show bgpcpp profiler' reachable and forwards it to
        # 'bgpcli show bgp profiler'.
        syntax = self._load()["commands"]["showBgpCpp"]["syntax"]
        self.assertIn("| profiler", syntax)

    def test_every_data_entry_is_a_well_formed_mapping(self) -> None:
        # A broken flow scalar can silently produce junk keys or non-mapping
        # values instead of the intended { keyword|<type>: { ... } } spec.
        data = self._load()["commands"]["showBgpCpp"]["data"]
        for name, spec in data.items():
            self.assertIsInstance(name, str, f"non-string token key: {name!r}")
            self.assertIsInstance(spec, dict, f"token {name!r} is not a mapping")

    def test_clear_bgpcpp_counters_command_present(self) -> None:
        # The 'clear bgpcpp counters' command is a Privileged (enable-mode)
        # entry; assert it parses and its tokens are well-formed so a stray flow
        # scalar does not silently drop it on the device.
        cmd = self._load()["commands"]["clearBgpCppCounters"]
        self.assertEqual("Privileged", cmd["mode"])
        data = cmd["data"]
        self.assertIn("counters", data)
        self.assertIn("<peerAddr>", data)
        for name, spec in data.items():
            self.assertIsInstance(name, str, f"non-string token key: {name!r}")
            self.assertIsInstance(spec, dict, f"token {name!r} is not a mapping")
