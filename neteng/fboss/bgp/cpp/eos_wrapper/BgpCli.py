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

# bgp++ integration with arista cli
# Reference: https://www.arista.com/en/support/toi/eos-4-25-2f/14712-cli-extensions-for-customers

import json
import os
import shutil
import socket
import struct
import subprocess
import sys
from itertools import islice
from subprocess import PIPE

# CliExtension and Tac are Arista EOS on-device runtime modules that are not
# available to the static type checker (the unit test stubs them at runtime via
# sys.modules). Suppress the unresolved-import error, matching the convention in
# nettools/ebb/drain_myself/DrainCli.py.
# pyrefly: ignore [missing-import]
import CliExtension

# pyrefly: ignore [missing-import]
import Tac


CONTAINER = "ebb"

ERROR_IGNORED_FILE = "ODSBatchedPublisher-inl.h"

# Drain states from DrainState enum in bgp_policy.thrift
# https://www.internalfb.com/code/configerator/source/neteng/bgp_policy/thrift/bgp_policy.thrift
DRAIN_STATE_MAP = {
    0: "UNDRAINED",
    1: "DRAINED",
    2: "WARM_DRAINED",
    3: "SOFT_DRAINED",
}

# BGP peer states from TBgpPeerState enum in bgp_thrift.thrift
# https://www.internalfb.com/code/fbsource/fbcode/neteng/fboss/bgp/if/bgp_thrift.thrift
PEER_STATE_MAP = {
    0: "IDLE",
    1: "CONNECT",
    2: "ACTIVE",
    3: "OPEN_SENT",
    4: "OPEN_CONFIRMED",
    5: "ESTABLISHED",
    6: "CLOSING",
    7: "IDLE_ADMIN",
}


def _ms_to_human(ms):
    secs = ms // 1000
    d, secs = divmod(secs, 86400)
    h, secs = divmod(secs, 3600)
    m, s = divmod(secs, 60)
    if d:
        return f"{d}d {h}h {m}m {s}s"
    if h:
        return f"{h}h {m}m {s}s"
    return f"{m}m {s}s"


def _convert_router_id(config):
    """Convert router ID from raw little-endian int to dotted IP string."""
    raw_rid = config.get("my_router_id", 0)
    if raw_rid:
        config["my_router_id"] = socket.inet_ntoa(struct.pack("<I", raw_rid))


def _convert_drain_state(host_data):
    """Replace drain_state integer with its DRAIN_STATE_MAP string in-place."""
    drain = host_data.get("drain_state", {})
    drain_state_val = drain.get("drain_state", 0)
    drain["drain_state"] = DRAIN_STATE_MAP.get(drain_state_val, "UNKNOWN")


def _convert_asn_fields(peer):
    """Fix AS number overflow by promoting *_4_byte fields and removing duplicates."""
    if "remote_as_4_byte" in peer:
        peer["remote_as"] = peer.pop("remote_as_4_byte")
    if "local_as_4_byte" in peer:
        peer["local_as"] = peer.pop("local_as_4_byte")


def _convert_peer_state(session):
    """Replace peer_state integer with its PEER_STATE_MAP string in-place.

    Returns the original integer peer_state for downstream use (e.g. to
    detect Established sessions for aggregate counters).
    """
    peer_state = session.get("peer", {}).get("peer_state", 0)
    session["peer_state"] = PEER_STATE_MAP.get(peer_state, "UNKNOWN")
    return peer_state


def _remove_legacy_prefix_counts(session):
    """Remove legacy prefix count fields that are always 0.

    The prepolicy_* and postpolicy_* fields contain the actual values.
    We remove the legacy fields to avoid confusion.
    """
    # Remove legacy fields that are always 0
    session.pop("rcvd_prefix_count", None)
    session.pop("sent_prefix_count", None)


def _add_human_readable_times(session, peer_state):
    """Add human-readable uptime/downtime strings."""
    uptime_ms = session.get("uptime", 0)
    if uptime_ms:
        session["uptime_str"] = _ms_to_human(uptime_ms)
    reset_time_ms = session.get("reset_time", 0)
    if peer_state != 5 and reset_time_ms:
        session["downtime_str"] = _ms_to_human(reset_time_ms)


def _set_session_defaults(session):
    """Set default fields that the server omits for IDLE peers."""
    session.setdefault("ingress_policy_name", "")
    session.setdefault("egress_policy_name", "")
    session.setdefault("update_group_id", 0)
    session.setdefault("peer_state_update_group", "NONE")
    session.setdefault("rib_version", 0)


def _process_session(session):
    """Process a single BGP session, fixing fields and computing stats.

    Returns:
        tuple: (is_established, prepolicy_rcvd, postpolicy_rcvd, postpolicy_sent)
    """
    peer = session.get("peer", {})

    _convert_asn_fields(peer)
    peer_state = _convert_peer_state(session)
    _remove_legacy_prefix_counts(session)
    _add_human_readable_times(session, peer_state)
    _set_session_defaults(session)

    prepolicy_rcvd = session.get("prepolicy_rcvd_prefix_count", 0)
    postpolicy_rcvd = session.get("postpolicy_rcvd_prefix_count", 0)
    postpolicy_sent = session.get("postpolicy_sent_prefix_count", 0)

    return peer_state == 5, prepolicy_rcvd, postpolicy_rcvd, postpolicy_sent


def _process_host_data(host_data):
    """Process host data for summary command, fixing fields and computing aggregates."""
    sessions = host_data.get("sessions", [])
    config = host_data.get("config", {})

    _convert_router_id(config)
    host_data["vrf"] = "default"
    _convert_drain_state(host_data)

    peers_up = 0
    peers_total = len(sessions)
    paths_received = 0
    paths_accepted = 0
    paths_sent = 0

    for session in sessions:
        is_established, prepolicy_rcvd, postpolicy_rcvd, postpolicy_sent = (
            _process_session(session)
        )
        if is_established:
            peers_up += 1
        paths_received += prepolicy_rcvd
        paths_accepted += postpolicy_rcvd
        paths_sent += postpolicy_sent

    host_data["peers_up"] = peers_up
    host_data["peers_total"] = peers_total
    host_data["paths_received"] = paths_received
    host_data["paths_accepted"] = paths_accepted
    host_data["paths_sent"] = paths_sent


def use_docker() -> bool:
    return shutil.which("bgpcli") is None


def _build_command(cmd, extra_env=None):
    """Build the full command list, prefixing with docker exec if needed.

    When running inside a container (bgpcli on PATH), env vars are passed
    via subprocess env dict. When running on the EOS host (bgpcli not on
    PATH), env vars are injected as docker exec -e flags so they reach
    the container process.
    """
    if not use_docker():
        return cmd
    prefix = ["docker", "exec"]
    for k, v in (extra_env or {}).items():
        prefix.extend(["-e", f"{k}={v}"])
    prefix.append(CONTAINER)
    return prefix + cmd


def capture_command(cmd, extra_env=None):
    """Run a command and return its stdout as a string.

    Unlike run_command(), this captures output instead of streaming it
    to the terminal, allowing callers to parse the result (e.g. as JSON).
    """
    full_cmd = _build_command(cmd, extra_env)
    env = None if use_docker() else os.environ | (extra_env or {})
    result = subprocess.run(
        full_cmd,
        capture_output=True,
        text=True,
        env=env,
    )
    return result.stdout


def run_command(cmd, extra_env=None):
    full_cmd = _build_command(cmd, extra_env)
    env = None if use_docker() else os.environ | (extra_env or {})
    stderr = Tac.run(
        full_cmd,
        stdin=sys.stdin,
        stdout=sys.stdout,
        stderr=PIPE,
        env=env,
    )

    for line in stderr.splitlines():
        if ERROR_IGNORED_FILE not in line:
            print(line, file=sys.stderr)


class ShowBgpCmd(CliExtension.ShowCommandClass):
    def _get_args(self, ctx):
        """Extract CLI arguments after 'show bgpcpp' from the context.

        Skips the first two positional args ('show' and 'bgpcpp') and
        flattens any list-valued arguments into individual strings.

        'summary' takes a 'sort-by <keys>' option, which the EOS grammar
        spells as a bare keyword while bgpcli wants '--sort-by'. Only the
        summary subcommand accepts it, so the rewrite is scoped to that
        subcommand rather than applied to every token.
        """
        args = []
        for arg in islice(ctx.args.values(), 2, None):
            if isinstance(arg, list):
                args.extend(str(a) for a in arg)
            else:
                args.append(str(arg))

        if args and args[0] == "summary":
            args = ["--sort-by" if arg == "sort-by" else arg for arg in args]
        return args

    def handler(self, ctx):
        """Return bgpcli output as parsed JSON for EOS '| json' support.

        Runs the bgpcli command with '--fmt json' and returns the parsed
        JSON dict. EOS uses this return value to serve '| json' pipe
        output. If the subcommand does not support JSON output, returns
        an empty dict so the command still succeeds.

        For the 'summary' subcommand, the raw JSON is post-processed via
        _process_host_data() to repair fields that bgpcli serializes
        incorrectly from the TBgpSession thrift struct and to add fields
        needed for parity with the text output. See the inline comment
        below for the full list.
        """
        self._args = self._get_args(ctx)
        output = capture_command(
            ["bgpcli", "--ssl-policy=plaintext", "--fmt", "json", "show", "bgp"]
            + self._args,
            {"LC_ALL": "C", "CONFIGERATOR_PRETEND_NOT_PROD": "1"},
        )
        try:
            data = json.loads(output)
        except (json.JSONDecodeError, TypeError):
            return {}

        if not (self._args and self._args[0] == "summary"):
            return data

        # Post-process summary JSON to fix fields that bgpcli serializes
        # incorrectly from the raw TBgpSession thrift struct and to add
        # derived fields needed for parity with the text output:
        # - my_router_id: raw little-endian int -> dotted IP string
        # - vrf: add (always "default" today)
        # - remote_as/local_as: i32 overflow for 4-byte ASNs -> promoted
        #   from *_4_byte fields (duplicates removed)
        # - rcvd/sent_prefix_count: legacy dead fields always 0 -> removed,
        #   use prepolicy_*/postpolicy_* fields instead
        # - peer_state: integer enum -> human-readable string (in-place)
        # - drain_state: integer enum -> human-readable string (in-place)
        # - uptime_str/downtime_str: human-readable duration strings
        # - peers_up/peers_total, paths_received/accepted/sent: computed
        #   summary aggregates across all sessions
        # - IDLE peer defaults: fill in fields the server omits for non-
        #   established peers (ingress/egress_policy_name, update_group_id,
        #   peer_state_update_group, rib_version)
        for _host_key, host_data in data.items():
            _process_host_data(host_data)

        return data

    def render(self, data):
        """Render human-readable text output by re-running bgpcli without --fmt json."""
        run_command(
            ["bgpcli", "--ssl-policy=plaintext", "show", "bgp"] + self._args,
            {"LC_ALL": "C", "CONFIGERATOR_PRETEND_NOT_PROD": "1"},
        )


class ShowBgpVersion(CliExtension.ShowCommandClass):
    """EOS show command for 'show bgpcpp version' with '| json' support."""

    def handler(self, ctx):
        """Return bgpcli version info as parsed JSON for EOS '| json' support."""
        output = capture_command(
            [
                "bgpcli",
                "--ssl-policy=plaintext",
                "--fmt",
                "json",
                "show",
                "version",
                "bgp",
            ],
            {"LC_ALL": "C", "CONFIGERATOR_PRETEND_NOT_PROD": "1"},
        )
        try:
            return json.loads(output)
        except (json.JSONDecodeError, TypeError):
            return {}

    def render(self, data):
        """Render human-readable version output by running bgpcli directly."""
        run_command(
            ["bgpcli", "--ssl-policy=plaintext", "show", "version", "bgp"],
            {"LC_ALL": "C", "CONFIGERATOR_PRETEND_NOT_PROD": "1"},
        )


class ShowBgpcppDrainState(CliExtension.ShowCommandClass):
    def handler(self, ctx):
        run_command(["drain_myself", "show-drain-state"])


class ClearBgpCppCounters(CliExtension.CliCommandClass):
    """EOS 'clear bgpcpp counters <peer>' -> bgpcli clear bgp counters <peer>.

    Clears the per-peer BGP message counters (socket Tx/Rx + AdjRib Sent/Recv).
    A peer address is required (enforced by the grammar); there is no clear-all
    form, so no interactive confirmation is needed.
    """

    def handler(self, ctx):
        # Tokens 0-2 are 'clear bgpcpp counters'; the remainder is the mandatory
        # peer IP address. Defensively reject a peerless invocation (the grammar
        # already requires <peerAddr>) so we never shell out a peerless
        # 'bgpcli clear bgp counters', which bgpcli rejects.
        peers = [str(arg) for arg in islice(ctx.args.values(), 3, None)]
        if not peers:
            raise ValueError("clear bgpcpp counters requires a peer IP address")

        cmd = ["bgpcli", "--ssl-policy=plaintext", "clear", "bgp", "counters"]
        cmd.extend(peers)

        run_command(cmd, {"LC_ALL": "C", "CONFIGERATOR_PRETEND_NOT_PROD": "1"})


def Plugin(ctx):
    CliExtension.registerCommand("showBgpCpp", ShowBgpCmd, namespace="meta.bgpcli")
    CliExtension.registerCommand(
        "showBgpVersion", ShowBgpVersion, namespace="meta.bgpcli"
    )
    CliExtension.registerCommand(
        "showBgpcppDrainState", ShowBgpcppDrainState, namespace="meta.bgpcli"
    )
    CliExtension.registerCommand(
        "clearBgpCppCounters", ClearBgpCppCounters, namespace="meta.bgpcli"
    )
