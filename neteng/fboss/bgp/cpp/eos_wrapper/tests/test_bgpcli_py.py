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

import sys
from unittest import TestCase
from unittest.mock import MagicMock, patch


# BgpCli imports Arista-only runtime modules (CliExtension, Tac) that do not
# exist in the test environment. Stub them so BgpCli can be imported. The
# command base classes must be real classes since BgpCli subclasses them.
_cli_extension_stub = MagicMock()
_cli_extension_stub.CliCommandClass = object
_cli_extension_stub.ShowCommandClass = object
sys.modules.setdefault("CliExtension", _cli_extension_stub)
sys.modules.setdefault("Tac", MagicMock())

# BgpCli.py is pulled in via its export (//neteng/fboss/bgp/cpp:eos_wrapper/
# BgpCli.py), which buck places under an eos_wrapper/ subdir of this test
# package -- hence the .eos_wrapper import path. The synthetic subpackage only
# exists in the buck-materialized runtime, so it is unresolvable to the static
# type checker.
# pyrefly: ignore [missing-import]
from .eos_wrapper import BgpCli


_MODULE = "neteng.fboss.bgp.cpp.eos_wrapper.tests.eos_wrapper.BgpCli"


def _ctx(*args: object) -> MagicMock:
    """Build a ctx whose .args mimics the EOS positional-arg ordered dict."""
    ctx = MagicMock()
    ctx.args = {str(i): a for i, a in enumerate(args)}
    return ctx


class ShowBgpCmdArgsTest(TestCase):
    """'show bgpcpp ...' token stream -> bgpcli argument list."""

    def test_plain_subcommand_passes_through(self) -> None:
        args = BgpCli.ShowBgpCmd()._get_args(_ctx("show", "bgpcpp", "summary"))
        self.assertEqual(args, ["summary"])

    def test_sort_by_keyword_becomes_a_flag(self) -> None:
        # The EOS grammar spells the option as a bare keyword; bgpcli wants
        # '--sort-by'. The value is forwarded untouched, commas and all.
        args = BgpCli.ShowBgpCmd()._get_args(
            _ctx("show", "bgpcpp", "summary", "sort-by", "pr:desc,peer")
        )
        self.assertEqual(args, ["summary", "--sort-by", "pr:desc,peer"])

    def test_sort_by_rewrite_is_scoped_to_summary(self) -> None:
        # Only 'summary' takes the option, so a same-named token anywhere else
        # is left alone rather than turned into a flag bgpcli would reject.
        args = BgpCli.ShowBgpCmd()._get_args(
            _ctx("show", "bgpcpp", "policy", "sort-by")
        )
        self.assertEqual(args, ["policy", "sort-by"])

    def test_list_valued_args_are_flattened(self) -> None:
        args = BgpCli.ShowBgpCmd()._get_args(
            _ctx("show", "bgpcpp", "table", "prefix", ["1.1.1.0/24", "2.2.2.0/24"])
        )
        self.assertEqual(args, ["table", "prefix", "1.1.1.0/24", "2.2.2.0/24"])


class ClearBgpCppCountersTest(TestCase):
    """clear bgpcpp counters -> bgpcli clear bgp counters wiring."""

    @patch(f"{_MODULE}.Tac")
    @patch(f"{_MODULE}.use_docker", return_value=False)
    def test_clear_specific_peer(
        self, _use_docker: MagicMock, mock_tac: MagicMock
    ) -> None:
        # A specific peer clears just that peer and needs no confirmation.
        mock_tac.run.return_value = ""
        BgpCli.ClearBgpCppCounters().handler(
            _ctx("clear", "bgpcpp", "counters", "2401:db00::1")
        )
        self.assertEqual(
            mock_tac.run.call_args[0][0],
            [
                "bgpcli",
                "--ssl-policy=plaintext",
                "clear",
                "bgp",
                "counters",
                "2401:db00::1",
            ],
        )

    @patch(f"{_MODULE}.Tac")
    @patch(f"{_MODULE}.use_docker", return_value=False)
    def test_clear_no_peer_raises(
        self, _use_docker: MagicMock, mock_tac: MagicMock
    ) -> None:
        # The grammar requires a peer address; a peerless invocation is rejected
        # in the handler rather than issuing a peerless (and rejected) bgpcli
        # call.
        with self.assertRaises(ValueError):
            BgpCli.ClearBgpCppCounters().handler(_ctx("clear", "bgpcpp", "counters"))
        mock_tac.run.assert_not_called()
