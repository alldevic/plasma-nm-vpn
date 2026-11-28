# Network changes and sleep (5.8): two parents, the veth stand-cli and the emulated Wi-Fi wlan0 (stand-up's wifi()),
# suspend, and core crashes under load. Kill switch on unless stated otherwise. Needs the VM's own kernel (kvm).
import subprocess
import threading
import time

import pytest

from conftest import sh
from helpers import core_pid, cursor, daemon_timers, fetch, ip4_dns, journal, ok, tun, units, wait_for
from test_60_killswitch import PROTOCOLS, connect, entry, kill_switch, make, names, succeeded  # noqa: F401

pytestmark = [pytest.mark.kvm, pytest.mark.test_build]

# The longest a parent change, a resume or a core crash may take until the tunnel is back (design.md, "MVP
# acceptance criteria"), from the measurements of 5.8 with a margin.
RECOVERY_S = 20
SERVER = "198.51.100.1"
LEAK = (f"not host {SERVER} and not host 2001:db8:100::1 and not (udp port 67 or udp port 68 or udp port 546 or "
        "udp port 547) and not icmp6 and not arp and not net 192.168.0.0/16 and not net 10.0.0.0/8 and "
        "not net 224.0.0.0/4 and not ip6 net fe80::/10 and not ip6 net ff00::/8")


@pytest.fixture(autouse=True)
def both_parents():
    """Both ways to the bench up before and after each case."""
    def up():
        for name in ("stand", "stand-wifi"):
            if sh(f"nmcli -g GENERAL.STATE connection show {name}", check=False).strip() != "activated":
                sh(f"nmcli --wait 30 connection up {name}", check=False)
        for name in ("stand", "stand-wifi"):
            assert sh(f"nmcli -g GENERAL.STATE connection show {name}").strip() == "activated", name
    if not ok("nmcli -g GENERAL.STATE connection show stand-wifi"):
        pytest.skip("no Wi-Fi on the bench (mac80211_hwsim)")
    up()
    yield
    up()


class Capture:
    """tcpdump of outgoing packets on both parents, for leaks."""

    def __init__(self, expr=LEAK):
        self.procs = [subprocess.Popen(["timeout", "300", "tcpdump", "-n", "-l", "-Q", "out", "-i", dev, expr],
                                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
                      for dev in ("stand-cli", "wlan0")]
        time.sleep(1)

    def lines(self):
        time.sleep(1)
        out = []
        for p in self.procs:
            p.terminate()
            out += [l for l in p.communicate()[0].splitlines() if l.strip()]
        return out


class Load:
    """A continuous curl to the bench and a dig loop, as an application would keep trying; and a 50/s ping, whose
    every packet takes the routes as they are at that moment, so a gap of a fraction of a second shows."""

    def __init__(self):
        self.procs = [subprocess.Popen("while true; do curl -s -m 2 http://203.0.113.1/ >/dev/null; "
                                       "dig +tries=1 +time=1 @203.0.113.1 stand.test >/dev/null; sleep 0.2; done",
                                       shell=True),
                      subprocess.Popen(["ping", "-q", "-i", "0.02", "203.0.113.1"], stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL)]

    def stop(self):
        for p in self.procs:
            p.kill()


def reconnected(since, uuid, timeout=RECOVERY_S + 10):
    """Seconds until the daemon reconnected, or None."""
    start = time.monotonic()
    if wait_for(lambda: f"[{uuid}] reconnected" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn"), timeout=timeout):
        return time.monotonic() - start
    return None


def gateway_device():
    out = sh(f"ip route show {SERVER}")
    return out.split(" dev ")[1].split()[0] if " dev " in out else None


def back_to_work(name):
    assert wait_for(lambda: sh(f"nmcli -g GENERAL.STATE connection show {name}").strip() == "activated", timeout=20)
    assert "pnv stand: internet" in fetch("http://203.0.113.1/")
    assert "203.0.113.1" in sh("dig +short +tries=2 +time=3 @203.0.113.1 stand.test A")


@pytest.mark.parametrize("protocol", PROTOCOLS)
def test_parent_device_change(names, protocol):
    names.append("pnv-n-switch")
    uuid = make(protocol, "pnv-n-switch")
    since = cursor()
    assert connect("pnv-n-switch").returncode == 0 and succeeded(since, uuid)
    assert gateway_device() == "stand-cli"
    cap = Capture()
    since = cursor()
    sh("nmcli device disconnect stand-cli")
    took = reconnected(since, uuid)
    assert took is not None and took <= RECOVERY_S, took
    assert gateway_device() == "wlan0"
    back_to_work("pnv-n-switch")
    since = cursor()
    sh("nmcli --wait 30 connection up stand && nmcli device disconnect wlan0")
    took = reconnected(since, uuid)
    assert took is not None and took <= RECOVERY_S, took
    assert gateway_device() == "stand-cli"
    back_to_work("pnv-n-switch")
    assert cap.lines() == []
    print(f"recovery {protocol}: parent change {took:.1f} s")


class States:
    """The states a connection goes through, polled every 0.1 s."""

    def __init__(self, name):
        self.seen, self.done = [], threading.Event()
        self.thread = threading.Thread(target=self.poll, args=(name,))
        self.thread.start()

    def poll(self, name):
        while not self.done.wait(0.1):
            state = sh(f"nmcli -g GENERAL.STATE connection show {name}", check=False).strip()
            if state and (not self.seen or self.seen[-1] != state):
                self.seen.append(state)

    def stop(self):
        self.done.set()
        self.thread.join()
        return self.seen


def test_dns_after_a_device_change(names):
    """The cycle sends a different DNS list (test hook) and shows the connection as connecting meanwhile. NM 1.58.1
    takes a new DNS list without STARTING as well (design.md), so the state is what fails when the cycle skips step 1
    (checked by hand with the hooks cycle-dns,skip-starting); the DNS list shows that the cycle's Ip4Config applied."""
    names.append("pnv-n-dns")
    uuid = make("hysteria2", "pnv-n-dns", pnv_test_hook="cycle-dns")
    since = cursor()
    assert connect("pnv-n-dns").returncode == 0 and succeeded(since, uuid)
    states = States("pnv-n-dns")
    since = cursor()
    sh("nmcli device disconnect stand-cli")
    assert reconnected(since, uuid) is not None
    assert wait_for(lambda: "203.0.113.53" in ip4_dns(tun(uuid)), timeout=10)
    assert wait_for(lambda: sh("nmcli -g GENERAL.STATE connection show pnv-n-dns").strip() == "activated", timeout=20)
    time.sleep(0.3)
    seen = states.stop()
    assert seen[0] == "activated" and "activating" in seen and seen[-1] == "activated", seen


@pytest.mark.parametrize("protocol", PROTOCOLS)
def test_no_leak_without_the_kill_switch(names, protocol):
    names.append("pnv-n-noks")
    uuid = make(protocol, "pnv-n-noks", kill_switch="no")
    since = cursor()
    assert connect("pnv-n-noks").returncode == 0 and succeeded(since, uuid)
    index = sh(f"cat /sys/class/net/{tun(uuid)}/ifindex").strip()
    load = Load()
    cap = Capture()
    since = cursor()
    sh("nmcli device disconnect stand-cli")
    assert reconnected(since, uuid) is not None
    since = cursor()
    sh(f"kill -9 {core_pid(uuid)}")
    assert reconnected(since, uuid) is not None
    time.sleep(2)
    load.stop()
    assert cap.lines() == []
    assert sh(f"cat /sys/class/net/{tun(uuid)}/ifindex").strip() == index


def test_device_comes_back_later(names):
    names.append("pnv-n-later")
    uuid = make("hysteria2", "pnv-n-later")
    since = cursor()
    assert connect("pnv-n-later").returncode == 0 and succeeded(since, uuid)
    since = cursor()
    sh("nmcli device disconnect stand-cli wlan0")
    time.sleep(10)
    sh("nmcli --wait 30 connection up stand-wifi")
    assert reconnected(since, uuid, timeout=60) is not None
    back_to_work("pnv-n-later")


def test_no_device_comes_back(names):
    names.append("pnv-n-never")
    uuid = make("hysteria2", "pnv-n-never")
    since = cursor()
    assert connect("pnv-n-never").returncode == 0 and succeeded(since, uuid)
    since = cursor()
    # A 20 s cycle instead of 120 s: the same give-up, sooner.
    with daemon_timers(cycle_timeout=20):
        sh("nmcli device disconnect stand-cli wlan0")
        assert wait_for(lambda: f"[{uuid}] giving up" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn"), timeout=60)
    assert "no parent device came back within 20 s" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn")
    assert wait_for(lambda: not units(uuid), timeout=20) and kill_switch(uuid) and entry(uuid)


@pytest.mark.parametrize("protocol", PROTOCOLS)
def test_sleep(names, protocol):
    names.append("pnv-n-sleep")
    uuid = make(protocol, "pnv-n-sleep")
    since = cursor()
    assert connect("pnv-n-sleep").returncode == 0 and succeeded(since, uuid)
    cap = Capture()
    since = cursor()
    sh("rtcwake -m no -s 20")
    sh("systemctl suspend")
    assert wait_for(lambda: "PM: suspend exit" in sh(f"journalctl -k --after-cursor '{since}' --no-pager",
                                                    check=False), timeout=120)
    resumed = time.monotonic()
    assert wait_for(lambda: f"[{uuid}] reconnected (trigger: resume" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn"),
                    timeout=RECOVERY_S + 30)
    took = time.monotonic() - resumed
    assert took <= RECOVERY_S, took
    back_to_work("pnv-n-sleep")
    assert cap.lines() == []
    print(f"recovery {protocol}: resume {took:.1f} s")


@pytest.mark.parametrize("protocol", PROTOCOLS)
def test_core_crash_under_load(names, protocol):
    names.append("pnv-n-crash")
    uuid = make(protocol, "pnv-n-crash")
    since = cursor()
    assert connect("pnv-n-crash").returncode == 0 and succeeded(since, uuid)
    load = Load()
    cap = Capture()
    since = cursor()
    sh(f"kill -9 {core_pid(uuid)}")
    took = reconnected(since, uuid)
    assert took is not None and took <= RECOVERY_S, took
    load.stop()
    back_to_work("pnv-n-crash")
    assert cap.lines() == [] and not entry(uuid)
    if protocol == "amneziawg":
        log = journal(since, "GLIB_DOMAIN=plasma-nm-vpn")
        assert "first handshake after" in log
        latest = int(sh(f"/usr/lib/plasma-nm-vpn/bin/awg show {tun(uuid)} latest-handshakes").split()[1])
        assert time.time() - latest < 60
    print(f"recovery {protocol}: core crash {took:.1f} s")


def test_repeated_failures_back_off(names):
    """A cycle whose attempts fail (the server unreachable for a while) waits longer each time."""
    names.append("pnv-n-backoff")
    uuid = make("hysteria2", "pnv-n-backoff", server=SERVER)
    since = cursor()
    assert connect("pnv-n-backoff").returncode == 0 and succeeded(since, uuid)
    n = "ip netns exec stand nft"
    sh(f"{n} add table inet tst-dead && {n} add chain inet tst-dead input '{{ type filter hook input priority -10; }}'"
       f" && {n} add rule inet tst-dead input udp dport 443 drop")
    try:
        since = cursor()
        sh(f"kill -9 {core_pid(uuid)}")
        assert wait_for(lambda: "next in 4 s" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn"), timeout=90)
    finally:
        sh(f"{n} destroy table inet tst-dead", check=False)
    log = journal(since, "GLIB_DOMAIN=plasma-nm-vpn")
    assert log.index("next in 1 s") < log.index("next in 2 s") < log.index("next in 4 s")
    assert reconnected(since, uuid, timeout=60) is not None
