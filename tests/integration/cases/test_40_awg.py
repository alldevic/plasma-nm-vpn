# The AmneziaWG daemon (3.1): amneziawg-go without root on the daemon's TUN, configured with awg setconf.
import os
import re
import subprocess
import time

import pytest

from conftest import sh
from helpers import core_pid, cursor, daemon_pid, journal, keyfile, no_traces, ok, scope_pids, tun, wait_for

pytestmark = pytest.mark.test_build

SERVICE = "org.freedesktop.NetworkManager.pnv.amneziawg"
CLIENT = os.path.join(os.path.dirname(__file__), "..", "stand", "secrets", "client", "awg-4.conf")
AWG = "/usr/lib/plasma-nm-vpn/bin/awg"
PARAMS = ("Jc", "Jmin", "Jmax", "S1", "S2", "S3", "S4", "H1", "H2", "H3", "H4", "I1", "ContentPaddingAddition")


def awg(name, **override):
    """A test profile from the bench's client config; keyword arguments replace vpn.data fields."""
    kv = dict(re.findall(r"^(\w+)\s*=\s*(.+?)\s*$", open(CLIENT).read(), re.M))
    host, port = kv["Endpoint"].rsplit(":", 1)
    data = {"server": host, "port": port, "address": "100.64.0.2/24", "peer-public-key": kv["PublicKey"],
            "allowed-ips": kv["AllowedIPs"], "keepalive": kv["PersistentKeepalive"], "dns": "203.0.113.1"}
    data.update({f"awg-{k.lower()}": kv[k] for k in PARAMS if k in kv})
    data.update({k.replace("_", "-"): v for k, v in override.items()})
    data = {k: v for k, v in data.items() if v != ""}
    secrets = {"private-key": kv["PrivateKey"], "awg-headerprotectionkey": kv["HeaderProtectionKey"]}
    return keyfile(name, data, secrets, service=SERVICE)


def connect(name):
    return subprocess.run(["nmcli", "--wait", "40", "connection", "up", name], capture_output=True, text=True)


def handshake(uuid):
    out = sh(f"{AWG} show {tun(uuid)} latest-handshakes", check=False).split()
    return int(out[1]) if len(out) == 2 else 0


@pytest.fixture
def names():
    made = []
    yield made
    for name in made:
        sh(f"nmcli connection down {name}", check=False)
        sh(f"nmcli connection delete {name}", check=False)


def test_connects_without_root(names):
    names.append("pnv-a-up")
    uuid = awg("pnv-a-up")
    assert connect("pnv-a-up").returncode == 0
    assert wait_for(lambda: handshake(uuid) > time.time() - 60, timeout=10)
    assert ok("ping -c1 -W3 100.64.0.1")
    assert sh(f"ps -o user= -p {core_pid(uuid)}").strip() == "plasma-nm-vpn"
    assert "persist on" in sh(f"ip -d link show {tun(uuid)}")
    assert sh(f"cat /sys/class/net/{tun(uuid)}/owner").strip() == sh("id -u plasma-nm-vpn").strip()
    assert not ok("command -v awg-quick") and not ok("test -e /usr/lib/plasma-nm-vpn/bin/awg-quick")


def test_first_handshake_without_keepalive_is_nudged(names):
    names.append("pnv-a-nudge")
    since = cursor()
    awg("pnv-a-nudge", keepalive="0")
    assert connect("pnv-a-nudge").returncode == 0
    assert wait_for(lambda: "first handshake after" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn"), timeout=15)


def test_wrong_peer_key_logs_no_handshake(names):
    names.append("pnv-a-wrong")
    since = cursor()
    uuid = awg("pnv-a-wrong", peer_public_key="MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIzNDU2Nzg5MDE=")
    assert connect("pnv-a-wrong").returncode == 0
    assert wait_for(lambda: "no handshake within 15 s" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn"), timeout=25)
    assert handshake(uuid) == 0


def socket_of(uuid):
    return f"/run/amneziawg/{tun(uuid)}.sock"


def test_nothing_left_after_down_and_kill_9(names):
    names.append("pnv-a-clean")
    uuid = awg("pnv-a-clean")
    assert connect("pnv-a-clean").returncode == 0
    assert ok(f"test -S {socket_of(uuid)}")
    sh("nmcli connection down pnv-a-clean")
    assert no_traces(uuid, server_ips=("198.51.100.1",)) == []
    assert not ok(f"test -e {socket_of(uuid)}")
    assert connect("pnv-a-clean").returncode == 0
    sh(f"kill -9 {[p for p in scope_pids(uuid) if p in daemon_pid()][0]}")
    assert no_traces(uuid, server_ips=("198.51.100.1",)) == []
    assert not ok(f"test -e {socket_of(uuid)}")


def test_stale_socket_is_removed(names):
    names.append("pnv-a-stale")
    uuid = awg("pnv-a-stale")
    sh(f"python3 -c \"import socket; socket.socket(socket.AF_UNIX).bind('{socket_of(uuid)}')\"")
    assert connect("pnv-a-stale").returncode == 0
    assert wait_for(lambda: handshake(uuid) > 0, timeout=10)
