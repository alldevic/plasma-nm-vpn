# The kill switch in CI (5.6): its chains and their lifecycle for all three protocols. The harness drives the VM
# over vsock, so the cases that block the network (allow-lan=no included) do not cut it off.
import subprocess
import time

import pytest

from conftest import sh
from helpers import (core_pid, cursor, daemon_pid, fetch, journal, no_traces, ok, run_dir_exists, scope_pids,
                     tun, tun_exists, units, wait_for)
from test_32_network import hy2
from test_40_awg import awg
from test_50_xray import vless

pytestmark = pytest.mark.test_build

PROTOCOLS = ["hysteria2", "amneziawg", "vless"]
SERVERS = ("198.51.100.1", "2001:db8:100::1")
# Direct traffic that a block allows: DHCP, DHCPv6, ND, MLD and local networks; and the core's own to the server.
ALLOWED = ("not host 198.51.100.1 and not host 2001:db8:100::1 and "
           "not (udp port 67 or udp port 68 or udp port 546 or udp port 547) and not icmp6 and "
           "not net 10.0.0.0/8 and not net 172.16.0.0/12 and not net 192.168.0.0/16 and not net 169.254.0.0/16 "
           "and not net 224.0.0.0/4 and not ip6 net fe80::/10 and not ip6 net ff00::/8 and not arp")


def make(protocol, name, **fields):
    """A kill-switch profile for the bench; keyword fields use _ for -."""
    # The bench's AmneziaWG client config has narrow allowed-ips; the kill switch is for default-route tunnels.
    defaults = {"allowed_ips": "0.0.0.0/0"} if protocol == "amneziawg" else {}
    fields = {"kill_switch": "yes", "dns": "203.0.113.1", **defaults, **fields}
    if protocol == "hysteria2":
        return hy2(name, data={k.replace("_", "-"): v for k, v in fields.items() if k != "dns"},
                   dns=fields["dns"])
    return (awg if protocol == "amneziawg" else vless)(name, **fields)


def connect(name, wait=40):
    return subprocess.run(["nmcli", "--wait", str(wait), "connection", "up", name], capture_output=True, text=True)


def kill_switch(uuid):
    return ok(f"nft list chain inet plasma_nm_vpn ks-out-{uuid}")


def guard(uuid):
    return ok(f"nft list chain inet plasma_nm_vpn guard-out-{uuid}")


def entry(uuid):
    return ok(f"grep -q '^\\[{uuid}\\]' /run/plasma-nm-vpn/guard-status")


def direct():
    return ok("ping -c1 -W2 -I stand-cli 192.0.2.1")


def daemon_of(uuid):
    return [p for p in scope_pids(uuid) if p in daemon_pid()][0]


def succeeded(since, uuid):
    return wait_for(lambda: f"[{uuid}] session succeeded" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn"), timeout=30)


def capture(expr):
    return subprocess.Popen(["timeout", "120", "tcpdump", "-n", "-l", "-Q", "out", "-i", "stand-cli", expr],
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)


def captured(cap):
    time.sleep(1)
    cap.terminate()
    return [l for l in cap.communicate()[0].splitlines() if l.strip()]


@pytest.fixture
def names():
    made = []
    yield made
    for name in made:
        sh(f"nmcli connection down {name}", check=False)
        sh(f"nmcli connection delete {name}", check=False)
    sh("plasma-nm-vpn-guard --clear", check=False)


@pytest.mark.parametrize("protocol", PROTOCOLS)
def test_core_crash_after_success_recovers(names, protocol):
    names.append("pnv-k-core")
    uuid = make(protocol, "pnv-k-core")
    since = cursor()
    assert connect("pnv-k-core").returncode == 0 and succeeded(since, uuid)
    since = cursor()
    sh(f"kill -9 {core_pid(uuid)}")
    assert wait_for(lambda: f"[{uuid}] reconnected" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn"), timeout=60)
    assert wait_for(lambda: sh("nmcli -g GENERAL.STATE connection show pnv-k-core").strip() == "activated", timeout=15)
    assert "pnv stand: internet" in fetch("http://203.0.113.1/")
    assert kill_switch(uuid) and not entry(uuid)


@pytest.mark.parametrize("protocol", PROTOCOLS)
@pytest.mark.parametrize("allow_lan", ["yes", "no"])
def test_daemon_crash_after_success_blocks(names, protocol, allow_lan):
    names.append("pnv-k-crash")
    uuid = make(protocol, "pnv-k-crash", allow_lan=allow_lan)
    since = cursor()
    assert connect("pnv-k-crash").returncode == 0 and succeeded(since, uuid)
    cap = capture(ALLOWED)
    time.sleep(1)
    sh(f"kill -9 {daemon_of(uuid)}")
    assert wait_for(lambda: not units(uuid), timeout=20)
    assert kill_switch(uuid) and not guard(uuid) and not tun_exists(uuid) and not run_dir_exists(uuid)
    assert entry(uuid)
    assert not direct()
    lan = ok("curl -s -m 3 http://192.168.77.1/")
    assert lan == (allow_lan == "yes")
    subprocess.run("curl -s -m 2 http://203.0.113.1/; dig +tries=1 +time=2 @192.0.2.1 stand.test", shell=True,
                   capture_output=True)
    assert captured(cap) == []
    # The next successful connect takes the block over and removes its entry.
    since = cursor()
    assert connect("pnv-k-crash").returncode == 0 and succeeded(since, uuid)
    assert wait_for(lambda: not entry(uuid), timeout=10)
    assert "inherited=true" not in sh(f"cat /run/plasma-nm-vpn/{uuid}/state")


def test_lan_dns_is_rejected_while_blocked(names):
    names.append("pnv-k-landns")
    uuid = make("hysteria2", "pnv-k-landns", allow_lan="yes")
    since = cursor()
    assert connect("pnv-k-landns").returncode == 0 and succeeded(since, uuid)
    sh(f"kill -9 {daemon_of(uuid)}")
    assert wait_for(lambda: not units(uuid), timeout=20) and kill_switch(uuid)
    cap = capture("dst net 192.168.0.0/16 and (port 53 or port 853)")
    time.sleep(1)
    start = time.monotonic()
    r = subprocess.run("dig +tries=1 +time=5 @192.168.77.1 stand.test", shell=True, capture_output=True, text=True)
    assert "refused" in r.stdout + r.stderr and time.monotonic() - start < 2
    start = time.monotonic()
    assert not ok("curl -s -m 5 telnet://192.168.77.1:853") and time.monotonic() - start < 2
    assert captured(cap) == []


def test_ipv6_only_server_with_the_kill_switch(names):
    names.append("pnv-k-v6")
    uuid = make("hysteria2", "pnv-k-v6", server="srv6.stand.test")
    since = cursor()
    assert connect("pnv-k-v6").returncode == 0 and succeeded(since, uuid)
    nd = capture("icmp6 and (ip6[40] == 135 or ip6[40] == 136) and ip6 dst 2001:db8:1::1")
    failed = False
    end = time.monotonic() + 130
    while time.monotonic() < end:
        assert "pnv stand: internet" in fetch("http://203.0.113.1/", timeout=10)
        failed = failed or "FAILED" in sh("ip -6 neigh show 2001:db8:1::1 dev stand-cli")
        time.sleep(5)
    assert not failed
    assert captured(nd), "no unicast neighbour discovery to the gateway"
    # The core's own last datagrams to the server, until the guard has stopped it, are exempt by design.
    cap = capture("ip6 and not icmp6 and not ip6 net fe80::/10 and not ip6 net ff00::/8 and not host 2001:db8:100::1")
    time.sleep(1)
    sh(f"kill -9 {daemon_of(uuid)}")
    assert wait_for(lambda: not units(uuid), timeout=20) and kill_switch(uuid)
    subprocess.run("curl -6 -s -m 2 http://[2001:db8:113::1]/", shell=True, capture_output=True)
    assert captured(cap) == []


@pytest.mark.parametrize("protocol", PROTOCOLS)
def test_nm_restart_keeps_the_block(names, protocol):
    names.append("pnv-k-nm")
    uuid = make(protocol, "pnv-k-nm")
    since = cursor()
    assert connect("pnv-k-nm").returncode == 0 and succeeded(since, uuid)
    sh("systemctl restart NetworkManager")
    assert wait_for(lambda: not units(uuid), timeout=30)
    assert kill_switch(uuid) and entry(uuid) and not direct()
    wait_for(lambda: ok("nmcli -t general status"), timeout=30)
    since = cursor()
    assert connect("pnv-k-nm").returncode == 0 and succeeded(since, uuid)
    assert wait_for(lambda: not entry(uuid), timeout=10)


@pytest.mark.parametrize("protocol", PROTOCOLS)
@pytest.mark.parametrize("how", ["delete", "down"])
def test_explicit_end_removes_the_block(names, protocol, how):
    names.append("pnv-k-end")
    uuid = make(protocol, "pnv-k-end")
    since = cursor()
    assert connect("pnv-k-end").returncode == 0 and succeeded(since, uuid)
    sh(f"nmcli connection {how} pnv-k-end")
    assert no_traces(uuid, server_ips=SERVERS) == []
    assert not kill_switch(uuid) and not entry(uuid) and direct()


@pytest.mark.parametrize("protocol", PROTOCOLS)
@pytest.mark.parametrize("failure", ["core-fails", "kill-daemon", "kill-core"])
def test_first_connection_failed(names, protocol, failure):
    names.append("pnv-k-first")
    hook = "broken-config" if failure == "core-fails" else "hold-before-config"
    uuid = make(protocol, "pnv-k-first", pnv_test_hook=hook)
    sh(f"rm -f /var/lib/plasma-nm-vpn/{uuid}")
    since = cursor()
    up = subprocess.Popen(["nmcli", "--wait", "60", "connection", "up", "pnv-k-first"], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True)
    if failure != "core-fails":
        assert wait_for(lambda: "holding Ip4Config" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn"), timeout=30)
        assert kill_switch(uuid)
        sh(f"kill -9 {daemon_of(uuid) if failure == 'kill-daemon' else core_pid(uuid)}")
    assert up.wait(timeout=90) != 0
    assert no_traces(uuid, server_ips=SERVERS) == []
    assert not kill_switch(uuid) and not entry(uuid)
    assert not ok(f"test -e /var/lib/plasma-nm-vpn/{uuid}")
    assert direct()
