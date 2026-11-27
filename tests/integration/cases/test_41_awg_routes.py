# AmneziaWG routes, DNS and the default-route rule (3.2), on the shared 2.7 helpers.
import subprocess
import time

import pytest

from conftest import DNS_MODE, sh
from helpers import core_pid, cursor, journal, no_traces, ok, scope_pids, daemon_pid, tun, units, wait_for, ip4_dns
from test_40_awg import awg, connect

pytestmark = pytest.mark.test_build

SERVER = ("198.51.100.1",)


def chains(uuid):
    return f"guard-out-{uuid}" in sh("nft list table inet plasma_nm_vpn 2>/dev/null || true")


def dns_of(uuid):
    return ip4_dns(tun(uuid))


@pytest.fixture
def names():
    made = []
    yield made
    for name in made:
        sh(f"nmcli connection down {name}", check=False)
        sh(f"nmcli connection delete {name}", check=False)


@pytest.mark.parametrize("allowed", ["0.0.0.0/0", "0.0.0.0/1, 128.0.0.0/1"])
def test_default_route_tunnel(names, allowed):
    names.append("pnv-r-default")
    uuid = awg("pnv-r-default", allowed_ips=allowed, dns="")
    assert connect("pnv-r-default").returncode == 0
    t = tun(uuid)
    assert f"dev {t}" in sh("ip route show default")
    assert "dev stand-cli" in sh("ip route show 198.51.100.1")
    assert "dev stand-cli" in sh("ip route get 198.51.100.1")
    assert dns_of(uuid) == "1.1.1.1 | 9.9.9.9"
    if DNS_MODE == "resolved":
        status = sh(f"resolvectl status {t}")
        assert "1.1.1.1" in status and ("~." in status or "Default Route: yes" in status)
    assert chains(uuid)
    assert "pnv stand: internet" in sh("curl -s -m 10 http://203.0.113.1/")


def test_nothing_leaks(names):
    names.append("pnv-r-leak")
    awg("pnv-r-leak", allowed_ips="0.0.0.0/0")
    assert connect("pnv-r-leak").returncode == 0
    # Outgoing only: a late packet from an earlier session's server is not a leak.
    cap = subprocess.Popen(["timeout", "30", "tcpdump", "-n", "-l", "-Q", "out", "-i", "stand-cli",
                            "(port 53 or port 853) or (ip6 and not dst net fe80::/10 and not dst net ff00::/8 "
                            "and not icmp6)"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    time.sleep(1)
    assert "pnv stand: internet" in sh("curl -s -m 10 http://stand.test/")
    subprocess.run("dig +tries=1 +time=2 @192.0.2.1 srv.stand.test; ping -6 -c1 -W2 2001:db8:1::1", shell=True,
                   capture_output=True)
    time.sleep(1)
    cap.terminate()
    assert cap.communicate()[0].strip() == ""


def test_never_default_and_narrow(names):
    names += ["pnv-r-nd", "pnv-r-narrow", "pnv-r-nd-dns"]
    uuid = awg("pnv-r-nd", allowed_ips="0.0.0.0/0", dns="")
    sh("nmcli connection modify pnv-r-nd ipv4.never-default yes")
    assert connect("pnv-r-nd").returncode == 0
    assert f"dev {tun(uuid)}" not in sh("ip route show default")
    assert not chains(uuid) and dns_of(uuid) == ""
    sh("nmcli connection down pnv-r-nd")

    narrow = awg("pnv-r-narrow", allowed_ips="100.64.0.0/24, 203.0.113.0/24", dns="")
    assert connect("pnv-r-narrow").returncode == 0
    t = tun(narrow)
    assert f"dev {t}" not in sh("ip route show default")
    assert not chains(narrow) and dns_of(narrow) == ""
    routes = sh(f"ip route show dev {t}")
    assert "100.64.0.0/24" in routes and "203.0.113.0/24" in routes
    assert "pnv stand: internet" in sh("curl -s -m 10 http://203.0.113.1/")
    sh("nmcli connection down pnv-r-narrow")

    with_dns = awg("pnv-r-nd-dns", allowed_ips="0.0.0.0/0", dns="203.0.113.1")
    sh("nmcli connection modify pnv-r-nd-dns ipv4.never-default yes")
    assert connect("pnv-r-nd-dns").returncode == 0
    assert f"dev {tun(with_dns)}" in sh("ip route get 203.0.113.1")
    assert dns_of(with_dns) == "203.0.113.1"
    assert "203.0.113.1" in sh("dig +short +tries=1 +time=3 @203.0.113.1 stand.test A")


def test_kill_9_of_the_core_before_success(names):
    names.append("pnv-r-corekill")
    uuid = awg("pnv-r-corekill", pnv_test_hook="hold-before-config")
    since = cursor()
    up = subprocess.Popen(["nmcli", "--wait", "60", "connection", "up", "pnv-r-corekill"], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True)
    assert wait_for(lambda: "holding Ip4Config" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn"), timeout=30)
    sh(f"kill -9 {core_pid(uuid)}")
    assert up.wait(timeout=60) != 0
    assert f"plasma-nm-vpn-core-{uuid}.service stopped" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn")
    assert no_traces(uuid, server_ips=SERVER) == []
    assert not ok(f"test -e /run/amneziawg/{tun(uuid)}.sock")


def test_up_again_after_kill_9_of_the_daemon(names):
    names.append("pnv-r-again")
    uuid = awg("pnv-r-again")
    assert connect("pnv-r-again").returncode == 0
    sh(f"kill -9 {[p for p in scope_pids(uuid) if p in daemon_pid()][0]}")
    assert no_traces(uuid, server_ips=SERVER) == []
    assert connect("pnv-r-again").returncode == 0
    assert ok("ping -c1 -W3 100.64.0.1")


@pytest.mark.parametrize("item,code", [("awg-jc=4\nPostUp=x", "control-char"), ("awg-foo=1", "unknown-key"),
                                       ("awg-postup=ip link del stand-cli", "unknown-key")])
def test_rejected_via_nmcli(names, item, code):
    names.append("pnv-r-reject")
    uuid = awg("pnv-r-reject")
    subprocess.run(["nmcli", "connection", "modify", "pnv-r-reject", "+vpn.data", item], check=True,
                   capture_output=True)
    since = cursor()
    assert connect("pnv-r-reject").returncode != 0
    assert f"profile rejected ({code})" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn")
    assert not units(uuid)
