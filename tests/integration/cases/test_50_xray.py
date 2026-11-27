# The xray daemon (4.2): VLESS XHTTP Reality to the bench, the TUN handed over as fd 3, no capabilities.
import os
import subprocess
import time

import pytest

from conftest import sh
from helpers import (core_pid, cursor, daemon_pid, fetch, journal, keyfile, no_traces, ok, scope_pids, tun, units,
                     wait_for)
from test_32_network import bench_secrets

pytestmark = pytest.mark.test_build

SERVICE = "org.freedesktop.NetworkManager.pnv.vless"
SERVER = ("198.51.100.1", "2001:db8:100::1")


def vless(name, **override):
    s = bench_secrets()
    data = {"server": "198.51.100.1", "port": "443", "reality-pbk": s["REALITY_PUBLIC"],
            "reality-sid": s["VLESS_SHORT_ID"], "tls-sni": "www.stand.test", "xhttp-path": s["VLESS_PATH"],
            "dns": "203.0.113.1"}
    data.update({k.replace("_", "-"): v for k, v in override.items()})
    return keyfile(name, data, {"uuid": s["VLESS_UUID"]}, service=SERVICE)


def connect(name):
    return subprocess.run(["nmcli", "--wait", "40", "connection", "up", name], capture_output=True, text=True)


@pytest.fixture
def names():
    made = []
    yield made
    for name in made:
        sh(f"nmcli connection down {name}", check=False)
        sh(f"nmcli connection delete {name}", check=False)


def test_tcp_and_udp_through_the_tunnel(names):
    names.append("pnv-x-up")
    uuid = vless("pnv-x-up")
    assert connect("pnv-x-up").returncode == 0
    assert "pnv stand: internet" in fetch("http://stand.test/")
    assert "203.0.113.1" in sh("dig +short +tries=2 +time=3 @203.0.113.1 stand.test A")
    pid = core_pid(uuid)
    assert sh(f"ps -o user= -p {pid}").strip() == "plasma-nm-vpn"
    assert sh(f"grep -E '^Cap(Eff|Prm):' /proc/{pid}/status").split()[1::2] == ["0000000000000000"] * 2
    assert "persist on" in sh(f"ip -d link show {tun(uuid)}")


def test_nothing_leaks(names):
    names.append("pnv-x-leak")
    vless("pnv-x-leak")
    assert connect("pnv-x-leak").returncode == 0
    cap = subprocess.Popen(["timeout", "30", "tcpdump", "-n", "-l", "-Q", "out", "-i", "stand-cli",
                            "(port 53 or port 853) or (ip6 and not dst net fe80::/10 and not dst net ff00::/8 "
                            "and not icmp6)"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    time.sleep(1)
    assert "pnv stand: internet" in fetch("http://stand.test/")
    subprocess.run("dig +tries=1 +time=2 @192.0.2.1 srv.stand.test; ping -6 -c1 -W2 2001:db8:1::1", shell=True,
                   capture_output=True)
    time.sleep(1)
    cap.terminate()
    assert cap.communicate()[0].strip() == ""


def test_nothing_left_after_down_and_kill_9(names):
    names.append("pnv-x-clean")
    uuid = vless("pnv-x-clean")
    assert connect("pnv-x-clean").returncode == 0
    sh("nmcli connection down pnv-x-clean")
    assert no_traces(uuid, server_ips=SERVER) == []
    assert connect("pnv-x-clean").returncode == 0
    sh(f"kill -9 {[p for p in scope_pids(uuid) if p in daemon_pid()][0]}")
    assert no_traces(uuid, server_ips=SERVER) == []
    assert connect("pnv-x-clean").returncode == 0


def test_kill_9_of_the_core_before_success(names):
    names.append("pnv-x-corekill")
    uuid = vless("pnv-x-corekill", pnv_test_hook="hold-before-config")
    since = cursor()
    up = subprocess.Popen(["nmcli", "--wait", "60", "connection", "up", "pnv-x-corekill"], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True)
    assert wait_for(lambda: "holding Ip4Config" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn"), timeout=30)
    sh(f"kill -9 {core_pid(uuid)}")
    assert up.wait(timeout=60) != 0
    assert f"plasma-nm-vpn-core-{uuid}.service stopped" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn")
    assert no_traces(uuid, server_ips=SERVER) == []


@pytest.mark.parametrize("item,code", [
    ('extra={"streamSettings":{"sockopt":{"dialerProxy":"direct"}}}', "extra-not-allowed"),
    ('xhttp-extra={"downloadSettings":{"address":"203.0.113.9"}}', "extra-not-allowed"),
    # No commas: nmcli splits +vpn.data items on them.
    ('extra={"mux":{"xudp_path":"/etc/x"}}', "extra-forbidden"),
    ("tls-sni=www.stand.test\nx", "control-char"),
    ("tls-fingerprint=unsafe", "bad-enum"),
])
def test_rejected_via_nmcli(names, item, code):
    names.append("pnv-x-reject")
    uuid = vless("pnv-x-reject")
    subprocess.run(["nmcli", "connection", "modify", "pnv-x-reject", "+vpn.data", item], check=True,
                   capture_output=True)
    since = cursor()
    assert connect("pnv-x-reject").returncode != 0
    assert f"profile rejected ({code})" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn")
    assert not units(uuid)
