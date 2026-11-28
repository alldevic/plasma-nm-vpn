# Blocks, clearing, liveness detection and simultaneous connections (5.7). Driven over vsock like 5.6.
import subprocess
import time

import pytest

from conftest import sh
from helpers import cursor, daemon_timers, fetch, ip4_dns, journal, ok, tun, units, wait_for
from test_60_killswitch import (PROTOCOLS, connect, daemon_of, direct, entry, kill_switch, make, names,  # noqa: F401
                                succeeded)

pytestmark = pytest.mark.test_build

N = "ip netns exec stand nft"


def gave_up(since, uuid, timeout=60):
    return wait_for(lambda: f"[{uuid}] giving up" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn"), timeout=timeout)


def block(protocol, name, **fields):
    """A block left by a successful session: kill -9 of its daemon."""
    uuid = make(protocol, name, **fields)
    since = cursor()
    assert connect(name).returncode == 0 and succeeded(since, uuid)
    sh(f"kill -9 {daemon_of(uuid)}")
    assert wait_for(lambda: not units(uuid), timeout=20) and kill_switch(uuid) and entry(uuid)
    return uuid


@pytest.fixture
def dead_server():
    """The bench servers stop answering: their ports are dropped in netns stand, no ICMP error either."""
    def stop():
        sh(f"{N} add table inet tst-dead && {N} add chain inet tst-dead input "
           "'{ type filter hook input priority -10; }' && "
           f"{N} add rule inet tst-dead input udp dport '{{ 443, 51820 }}' drop && "
           f"{N} add rule inet tst-dead input tcp dport 443 drop")
    yield stop
    sh(f"{N} destroy table inet tst-dead", check=False)


@pytest.mark.parametrize("protocol", PROTOCOLS)
def test_inherited_block_survives(names, dead_server, protocol):
    names.append("pnv-b-inh")
    uuid = block(protocol, "pnv-b-inh")
    dead_server()
    samples = subprocess.Popen(f"for i in $(seq 100); do nft list table inet plasma_nm_vpn | grep -c 'chain ks-out-{uuid}'; "
                               "sleep 0.1; done", shell=True, stdout=subprocess.PIPE, text=True)
    dns = subprocess.Popen(["timeout", "40", "tcpdump", "-n", "-l", "-Q", "out", "-i", "stand-cli", "port 53"],
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    since = cursor()
    # The attempt's liveness check fails after 5 s instead of 15, well within the 10 s of samples.
    with daemon_timers(liveness_timeout=5):
        connect("pnv-b-inh")
        assert gave_up(since, uuid)
    log = journal(since, "GLIB_DOMAIN=plasma-nm-vpn")
    assert "working under it (inherited block)" in log
    assert protocol != "hysteria2" or "under the inherited block: using the cached IP" in log
    assert set(samples.communicate()[0].split()) == {"1"}
    dns.terminate()
    assert [l for l in dns.communicate()[0].splitlines() if "?" in l] == []
    assert wait_for(lambda: not units(uuid), timeout=20) and kill_switch(uuid) and entry(uuid)
    # Held, then an explicit disconnect: still blocked.
    sh("nmcli connection modify pnv-b-inh +vpn.data pnv-test-hook=hold-before-config")
    since = cursor()
    up = subprocess.Popen(["nmcli", "--wait", "60", "connection", "up", "pnv-b-inh"], stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL)
    assert wait_for(lambda: "holding Ip4Config" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn"), timeout=30)
    sh("nmcli connection down pnv-b-inh")
    up.wait(timeout=60)
    assert wait_for(lambda: not units(uuid), timeout=20) and kill_switch(uuid) and entry(uuid)
    # No cache: the attempt fails at once and the block stays (an IP literal needs none: VLESS and AmneziaWG).
    if protocol == "hysteria2":
        sh("nmcli connection modify pnv-b-inh -vpn.data pnv-test-hook")
        sh(f"rm -f /var/lib/plasma-nm-vpn/{uuid}")
        since = cursor()
        start = time.monotonic()
        assert connect("pnv-b-inh").returncode != 0 and time.monotonic() - start < 20
        assert "no cached IP for" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn")
        assert kill_switch(uuid) and entry(uuid)


def test_quick_down_and_up(names):
    """No re-activation detection (decided before 5.2): NM refuses an up of an active profile, and a quick
    down/up is an explicit disconnect followed by a new session with chains of its own, with no block shown."""
    names.append("pnv-b-react")
    uuid = make("hysteria2", "pnv-b-react")
    since = cursor()
    assert connect("pnv-b-react").returncode == 0 and succeeded(since, uuid)
    r = connect("pnv-b-react")
    assert r.returncode != 0 and "already active" in r.stderr
    samples = subprocess.Popen("for i in $(seq 100); do grep -c '^\\[' /run/plasma-nm-vpn/guard-status 2>/dev/null "
                               "|| echo 0; sleep 0.1; done", shell=True, stdout=subprocess.PIPE, text=True)
    since = cursor()
    sh("nmcli connection down pnv-b-react")
    assert connect("pnv-b-react").returncode == 0 and succeeded(since, uuid)
    assert set(samples.communicate()[0].split()) == {"0"}
    assert kill_switch(uuid) and not entry(uuid)
    assert "inherited=true" not in sh(f"cat /run/plasma-nm-vpn/{uuid}/state")


@pytest.fixture
def moved_server():
    """The server moves from 198.51.100.1 to 198.51.100.2 (a DNAT to the same server in netns stand), and the name
    follows (/etc/hosts, ahead of the "ISP" DNS for the system resolver)."""
    def move():
        sh(f"{N} add table ip tst-move && "
           f"{N} add chain ip tst-move raw '{{ type filter hook prerouting priority -300; }}' && "
           f"{N} add rule ip tst-move raw iifname stand-srv ip daddr 198.51.100.1 drop && "
           f"{N} add chain ip tst-move nat '{{ type nat hook prerouting priority dstnat; }}' && "
           f"{N} add rule ip tst-move nat ip daddr 198.51.100.2 dnat to 198.51.100.1")
        sh("cp /etc/hosts /tmp/hosts.pnv && echo '198.51.100.2 srv.stand.test' >> /etc/hosts")
    yield move
    sh(f"{N} destroy table ip tst-move", check=False)
    sh("test -f /tmp/hosts.pnv && cp /tmp/hosts.pnv /etc/hosts", check=False)


@pytest.fixture
def polkit_consents():
    """Stands in for the administrator answering the polkit prompt for the regular user."""
    rule = "/etc/polkit-1/rules.d/49-pnv-test.rules"
    with open(rule, "w") as f:
        f.write('polkit.addRule(function(a, s) { if (a.id == "io.github.alldevic.plasma-nm-vpn.guard" && '
                's.user == "pnv-test") return polkit.Result.YES; });\n')
    sh("id pnv-test >/dev/null 2>&1 || useradd --no-create-home pnv-test")
    time.sleep(1)
    yield
    sh(f"rm -f {rule}")


def as_user(cmd):
    return subprocess.run(["runuser", "-u", "pnv-test", "--", *cmd], capture_output=True, text=True,
                          stdin=subprocess.DEVNULL)


def test_server_ip_changed(names, moved_server, polkit_consents):
    names.append("pnv-b-move")
    uuid = block("hysteria2", "pnv-b-move")
    moved_server()
    since = cursor()
    connect("pnv-b-move")
    assert gave_up(since, uuid)
    assert "using the cached IP 198.51.100.1" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn")
    assert wait_for(lambda: not units(uuid), timeout=20) and kill_switch(uuid) and entry(uuid)
    assert as_user(["plasma-nm-vpn-guard", "--clear", uuid]).returncode == 0
    assert not kill_switch(uuid) and not entry(uuid)
    since = cursor()
    assert connect("pnv-b-move").returncode == 0 and succeeded(since, uuid)
    assert "server IP 198.51.100.2" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn")
    assert wait_for(lambda: "ip=198.51.100.2" in sh(f"cat /var/lib/plasma-nm-vpn/{uuid}"), timeout=10)


def test_bare_clear(names, polkit_consents):
    names += ["pnv-b-live", "pnv-b-one", "pnv-b-two"]
    live = make("hysteria2", "pnv-b-live", server="198.51.100.1")
    since = cursor()
    assert connect("pnv-b-live").returncode == 0 and succeeded(since, live)
    one, two = block("vless", "pnv-b-one"), block("amneziawg", "pnv-b-two")
    sh("rm -f /etc/polkit-1/rules.d/49-pnv-test.rules")
    time.sleep(1)
    refused = as_user(["plasma-nm-vpn-guard", "--clear"])
    assert refused.returncode != 0 and kill_switch(one) and kill_switch(two)
    polkit_rule = ('polkit.addRule(function(a, s) { if (a.id == "io.github.alldevic.plasma-nm-vpn.guard" && '
                   's.user == "pnv-test") return polkit.Result.YES; });\n')
    with open("/etc/polkit-1/rules.d/49-pnv-test.rules", "w") as f:
        f.write(polkit_rule)
    time.sleep(1)
    assert as_user(["plasma-nm-vpn-guard", "--clear"]).returncode == 0
    assert not kill_switch(one) and not kill_switch(two) and not entry(one) and not entry(two)
    assert kill_switch(live) and "pnv stand: internet" in fetch("http://203.0.113.1/")


WRONG = {"hysteria2": {"password": "wrong"}, "vless": {"uuid": "00000000-1111-4222-8333-444444444444"},
         "amneziawg": {"private-key": "4KSwMmT7m1tWQ8m3rRmQqRQGqCmY6gfnnu5M7pH1OVo="}}


@pytest.mark.parametrize("protocol", PROTOCOLS)
@pytest.mark.parametrize("why", ["wrong-secret", "dead-server"])
def test_liveness_detects_a_bad_first_connection(names, dead_server, protocol, why):
    names.append("pnv-b-live")
    if why == "dead-server":
        dead_server()
        uuid = make(protocol, "pnv-b-live")
    else:
        uuid = make(protocol, "pnv-b-live")
        for key, value in WRONG[protocol].items():
            sh(f"sed -i '/^\\[vpn-secrets\\]/,/^\\[/ s|^{key}=.*|{key}={value}|' "
               "/etc/NetworkManager/system-connections/pnv-b-live.nmconnection")
        sh("nmcli connection load /etc/NetworkManager/system-connections/pnv-b-live.nmconnection")
    sh(f"rm -f /var/lib/plasma-nm-vpn/{uuid}")
    since = cursor()
    start = time.monotonic()
    with daemon_timers(liveness_timeout=5):
        connect("pnv-b-live")
        assert gave_up(since, uuid, timeout=40) and time.monotonic() - start < 40
    assert "the liveness check failed" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn")
    assert wait_for(lambda: not units(uuid), timeout=20)
    assert not kill_switch(uuid) and not entry(uuid) and not ok(f"test -e /var/lib/plasma-nm-vpn/{uuid}")
    assert direct()


def test_dead_server_after_success_gives_up_after_the_cycle(names, dead_server):
    names.append("pnv-b-dead")
    uuid = make("hysteria2", "pnv-b-dead", server="198.51.100.1")
    # Checks every 5 s, failing after 5 s each, and a 30 s cycle: the same sequence as 60, 15 and 120 s, in a minute.
    with daemon_timers(liveness_interval=5, liveness_timeout=5, cycle_timeout=30):
        since = cursor()
        assert connect("pnv-b-dead").returncode == 0 and succeeded(since, uuid)
        dead_server()
        assert wait_for(lambda: f"[{uuid}] reconnect step 1" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn"),
                        timeout=60)
        assert gave_up(since, uuid, timeout=60)
    assert "reconnecting failed within 30 s" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn")
    assert wait_for(lambda: not units(uuid), timeout=20) and kill_switch(uuid) and entry(uuid)
    time.sleep(15)
    assert not units(uuid), "NM reconnected on its own"


def test_two_simultaneous_connections(names):
    names += ["pnv-b-def", "pnv-b-nd"]
    default = make("hysteria2", "pnv-b-def", server="198.51.100.1")
    narrow = make("vless", "pnv-b-nd")
    sh("nmcli connection modify pnv-b-nd ipv4.never-default yes")
    for name, uuid in (("pnv-b-def", default), ("pnv-b-nd", narrow)):
        since = cursor()
        assert connect(name).returncode == 0 and succeeded(since, uuid)
    assert kill_switch(default) and not kill_switch(narrow)
    assert f"dev {tun(narrow)}" in sh("ip route get 203.0.113.1")
    assert "pnv stand: internet" in fetch("http://203.0.113.1/")
    assert "203.0.113.1" in sh("dig +short +tries=2 +time=3 @203.0.113.1 stand.test A")
    assert "pnv stand: internet" in fetch("http://stand.test/")
    sh("nmcli connection down pnv-b-nd")
    assert "pnv stand: internet" in fetch("http://203.0.113.1/") and kill_switch(default)


def test_1_1_1_1_is_not_taken_over(names):
    names += ["pnv-b-nd1", "pnv-b-def1"]
    narrow = make("vless", "pnv-b-nd1", kill_switch="no", dns="")
    sh("nmcli connection modify pnv-b-nd1 ipv4.never-default yes")
    default = make("hysteria2", "pnv-b-def1", server="198.51.100.1", dns="")
    for name, uuid in (("pnv-b-nd1", narrow), ("pnv-b-def1", default)):
        since = cursor()
        assert connect(name).returncode == 0 and succeeded(since, uuid)
    assert f"dev {tun(narrow)}" not in sh("ip route get 1.1.1.1")
    assert ip4_dns(tun(narrow)) == ""
    cap = subprocess.Popen(["timeout", "20", "tcpdump", "-n", "-l", "-i", tun(narrow), "host 1.1.1.1"],
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    time.sleep(1)
    subprocess.run("for i in 1 2 3; do dig +tries=1 +time=2 @1.1.1.1 stand.test; done", shell=True,
                   capture_output=True)
    time.sleep(1)
    cap.terminate()
    # Only the never-default tunnel's own liveness queries (". SOA") may appear on it.
    assert [l for l in cap.communicate()[0].splitlines() if "stand.test" in l] == []
