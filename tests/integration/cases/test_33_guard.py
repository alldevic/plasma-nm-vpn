# The DNS and IPv6 guard (2.6): nothing to 53/853 and no IPv6 bypasses a default-route tunnel.
import subprocess
import time

import pytest

from conftest import DNS_MODE, sh
from helpers import cursor, daemon_pid, gone, journal, ok, scope_pids, tun, wait_for
from test_32_network import connect, hy2

pytestmark = pytest.mark.test_build


def chains(uuid):
    out = sh("nft list table inet plasma_nm_vpn 2>/dev/null || true")
    return [c for c in (f"guard-out-{uuid}", f"guard-fwd-{uuid}") if f"chain {c} " in out]


def fails_fast(cmd):
    start = time.monotonic()
    r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=30)
    return r.returncode != 0 and time.monotonic() - start < 2, r.stdout + r.stderr


def capture(expr):
    return subprocess.Popen(["timeout", "30", "tcpdump", "-n", "-l", "-i", "stand-cli", expr],
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)


def captured(cap):
    time.sleep(1)
    cap.terminate()
    return cap.communicate()[0].strip()


@pytest.fixture
def names():
    made = []
    yield made
    for name in made:
        sh(f"nmcli connection down {name}", check=False)
        sh(f"nmcli connection delete {name}", check=False)


@pytest.fixture
def default_tunnel(names):
    names.append("pnv-g-default")
    uuid = hy2("pnv-g-default")
    assert connect("pnv-g-default").returncode == 0
    assert chains(uuid) == [f"guard-out-{uuid}", f"guard-fwd-{uuid}"]
    return uuid


def test_nothing_leaks_past_the_tunnel(default_tunnel):
    cap = capture("(port 53 or port 853) or (ip6 and not dst net fe80::/10 and not dst net ff00::/8 and not icmp6)")
    time.sleep(1)
    assert "pnv stand: internet" in sh("curl -s -m 5 http://stand.test/")
    sh("getent hosts srv.stand.test")
    subprocess.run("dig +tries=1 +time=2 @192.0.2.1 srv.stand.test; ping -6 -c1 -W2 2001:db8:1::1", shell=True,
                   capture_output=True)
    assert captured(cap) == ""


def test_dns_and_ipv6_are_rejected_not_dropped(default_tunnel):
    fast, out = fails_fast("dig +tries=1 +time=5 @192.0.2.1 srv.stand.test")
    assert fast and "refused" in out, out
    fast, out = fails_fast("ping -6 -c1 -W5 2001:db8:1::1")
    assert fast, out
    assert "stand.test" in sh("getent hosts stand.test")
    if DNS_MODE == "resolved":
        assert "203.0.113.1" in sh("resolvectl query -4 stand.test")
    else:
        first = [l.split()[1] for l in open("/etc/resolv.conf") if l.startswith("nameserver")][0]
        assert first == "203.0.113.1"


def test_neighbour_probes_to_global_addresses_pass(names):
    names.append("pnv-g-v6")
    uuid = hy2("pnv-g-v6", server="srv6.stand.test")
    assert connect("pnv-g-v6").returncode == 0
    assert chains(uuid)
    cap = capture("icmp6 and ip6[40] == 135 and ip6 dst 2001:db8:1::1")
    sh("ip -6 neigh change 2001:db8:1::1 dev stand-cli nud stale")
    time.sleep(1)
    sh("curl -s -m 5 http://stand.test/")
    assert wait_for(lambda: "REACHABLE" in sh("ip -6 neigh show 2001:db8:1::1 dev stand-cli"), timeout=15)
    assert "2001:db8:1::1" in captured(cap)


@pytest.fixture
def forwarding():
    sh("ip netns add tst-fwd && ip link add tst-fwd0 type veth peer name tst-fwd1 && "
       "ip link set tst-fwd1 netns tst-fwd && ip addr add 10.77.0.1/30 dev tst-fwd0 && ip link set tst-fwd0 up && "
       "ip -n tst-fwd addr add 10.77.0.2/30 dev tst-fwd1 && ip -n tst-fwd link set tst-fwd1 up && "
       "ip -n tst-fwd route add default via 10.77.0.1")
    old = sh("sysctl -n net.ipv4.ip_forward").strip()
    sh("sysctl -qw net.ipv4.ip_forward=1")
    sh("nft add table ip tst-nat && nft add chain ip tst-nat post '{ type nat hook postrouting priority srcnat; }' && "
       "nft add rule ip tst-nat post ip saddr 10.77.0.0/30 oifname stand-cli masquerade")
    yield
    sh("nft delete table ip tst-nat", check=False)
    sh(f"sysctl -qw net.ipv4.ip_forward={old}")
    sh("ip link del tst-fwd0", check=False)
    sh("ip netns del tst-fwd", check=False)


def fwd_dns_rejects(uuid):
    out = sh(f"nft list chain inet plasma_nm_vpn guard-fwd-{uuid}")
    line = [l for l in out.splitlines() if "udp dport { 53, 853 }" in l][0]
    return int(line.split("counter packets ")[1].split()[0])


def test_forwarded_dns_is_rejected(default_tunnel, forwarding):
    before = fwd_dns_rejects(default_tunnel)
    fast, out = fails_fast("ip netns exec tst-fwd dig +tries=1 +time=5 @192.0.2.1 srv.stand.test")
    assert fast and "refused" in out, out
    assert fwd_dns_rejects(default_tunnel) > before


def test_chains_go_with_the_session(names):
    names.append("pnv-g-down")
    uuid = hy2("pnv-g-down")
    assert connect("pnv-g-down").returncode == 0
    assert chains(uuid)
    sh("nmcli connection down pnv-g-down")
    assert wait_for(lambda: gone(uuid) and not chains(uuid), timeout=20)
    assert connect("pnv-g-down").returncode == 0
    since = cursor()
    pid = [p for p in scope_pids(uuid) if p in daemon_pid()][0]
    sh(f"kill -9 {pid}")
    assert wait_for(lambda: gone(uuid) and not chains(uuid), timeout=15)
    assert "cleaned up" in journal(since, f"-u plasma-nm-vpn-guard-{uuid}.service")


def test_a_second_tunnel_is_not_blocked(default_tunnel, names):
    names.append("pnv-g-second")
    second = hy2("pnv-g-second", never_default=True)
    assert connect("pnv-g-second").returncode == 0
    assert not chains(second)
    assert f"dev {tun(second)}" in sh("ip route get 203.0.113.1")
    assert "203.0.113.1" in sh("dig +short +tries=1 +time=3 @203.0.113.1 stand.test A")
    assert "pnv stand: internet" in sh("curl -s -m 5 http://203.0.113.1/")


def test_never_default_has_no_guard(names):
    names.append("pnv-g-nd")
    uuid = hy2("pnv-g-nd", never_default=True, dns=None)
    assert connect("pnv-g-nd").returncode == 0
    assert not chains(uuid)
    assert "198.51.100.1" in sh("dig +short +tries=1 +time=3 @192.0.2.1 srv.stand.test A")
    assert "srv.stand.test" in sh("getent hosts srv.stand.test")
