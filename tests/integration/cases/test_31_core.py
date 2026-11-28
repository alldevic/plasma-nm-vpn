# Core launch (2.3): units, the guard, readiness, synchronous cleanup, orphans, re-activation, TUN name clashes.
import subprocess
import time

import pytest

from conftest import sh
from helpers import (core_pid, cursor, daemon_pid, gone, journal, ok, profile, run_dir_exists, scope_pids, tun,
                     tun_exists, units, uuid_of, wait_for)

pytestmark = pytest.mark.test_build


def connect(name, wait=30):
    return subprocess.run(["nmcli", "--wait", str(wait), "connection", "up", name], capture_output=True, text=True)


def chain_exists(uuid):
    return ok(f"nft list chain inet plasma_nm_vpn test-{uuid[:8]}")


def daemon_of(uuid):
    pids = [p for p in scope_pids(uuid) if p in daemon_pid()]
    return pids[0] if pids else None


@pytest.fixture
def active():
    """A connected profile; whatever a case leaves behind is disconnected and deleted."""
    names = []

    def make(name, **kw):
        names.append(profile(name, password="pw", **kw))
        r = connect(name)
        assert r.returncode == 0, r.stdout + r.stderr
        return uuid_of(name)

    yield make
    for name in names:
        sh(f"nmcli connection down {name}", check=False)
        sh(f"nmcli connection delete {name}", check=False)


def test_up_and_down(active):
    uuid = active("pnv-c-updown")
    u = units(uuid)
    assert u.get(f"plasma-nm-vpn-core-{uuid}.service") == "active"
    assert u.get(f"plasma-nm-vpn-guard-{uuid}.service") == "active"
    assert u.get(f"plasma-nm-vpn-daemon-{uuid}.scope") == "active"
    assert sh(f"ps -o user= -p {core_pid(uuid)}").strip() == "plasma-nm-vpn"
    assert tun_exists(uuid)
    assert sh(f"stat -c '%a %U' /run/plasma-nm-vpn /run/plasma-nm-vpn/{uuid}").split() == ["755", "root", "700", "root"]
    assert chain_exists(uuid)
    sh("nmcli connection down pnv-c-updown")
    assert wait_for(lambda: gone(uuid) and not daemon_pid(), timeout=20)
    assert not chain_exists(uuid)


def test_broken_config_fails_with_the_unit_name():
    profile("pnv-c-broken", data="server=198.51.100.1, ports=443, pnv-test-hook=broken-config", password="pw")
    uuid = uuid_of("pnv-c-broken")
    since = cursor()
    start = time.monotonic()
    r = connect("pnv-c-broken", wait=60)
    assert r.returncode != 0
    assert time.monotonic() - start < 30
    assert f"plasma-nm-vpn-core-{uuid}.service" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn")
    assert wait_for(lambda: gone(uuid), timeout=20)
    assert not chain_exists(uuid)
    sh("nmcli connection delete pnv-c-broken")


def test_kill_9_of_the_daemon(active):
    uuid = active("pnv-c-kill")
    since = cursor()
    sh(f"kill -9 {daemon_of(uuid)}")
    assert wait_for(lambda: gone(uuid), timeout=15)
    guard = journal(since, f"-u plasma-nm-vpn-guard-{uuid}.service")
    assert "cleaned up" in guard


def test_kill_9_then_up_waits_for_the_old_guard(active):
    uuid = active("pnv-c-rekill")
    sh(f"kill -9 {daemon_of(uuid)}")
    r = connect("pnv-c-rekill")
    assert r.returncode == 0, r.stdout + r.stderr
    assert units(uuid).get(f"plasma-nm-vpn-core-{uuid}.service") == "active"


def test_leftovers_are_removed():
    name = profile("pnv-c-left", password="pw")
    uuid = uuid_of(name)
    sh(f"install -d -m 0700 /run/plasma-nm-vpn/{uuid} && touch /run/plasma-nm-vpn/{uuid}/config.json")
    sh(f"ip tuntap add dev {tun(uuid)} mode tun")
    sh(f"systemd-run --unit plasma-nm-vpn-core-{uuid}.service --property CollectMode=inactive /bin/false")
    try:
        r = connect(name)
        assert r.returncode == 0, r.stdout + r.stderr
        assert units(uuid).get(f"plasma-nm-vpn-core-{uuid}.service") == "active"
    finally:
        sh(f"nmcli connection down {name}", check=False)
        sh(f"nmcli connection delete {name}")


def test_up_of_an_active_profile_is_refused_by_nm(active):
    """NM 1.58.1 does not re-activate an active VPN profile (0.7): the live instance is left alone."""
    uuid = active("pnv-c-react")
    core = core_pid(uuid)
    r = connect("pnv-c-react")
    assert r.returncode != 0 and "already active" in r.stderr
    assert core_pid(uuid) == core
    assert units(uuid).get(f"plasma-nm-vpn-core-{uuid}.service") == "active"


def test_reactivation_while_the_old_instance_hangs(active):
    uuid = active("pnv-c-stop")
    old = daemon_of(uuid)
    core = core_pid(uuid)
    since = cursor()
    sh(f"kill -STOP {old}")
    try:
        # NM deactivates at once without waiting for the stopped daemon, then starts a new instance.
        sh("nmcli connection down pnv-c-stop")
        r = connect("pnv-c-stop", wait=45)
        assert r.returncode != 0
        log = journal(since, "GLIB_DOMAIN=plasma-nm-vpn")
        assert "the previous instance is still active: waiting for it" in log
        assert "still up after 10 s" in log
        assert core_pid(uuid) == core
        assert tun_exists(uuid) and run_dir_exists(uuid)
        assert units(uuid).get(f"plasma-nm-vpn-daemon-{uuid}.scope") == "active"
    finally:
        sh(f"kill -CONT {old}")
    assert wait_for(lambda: gone(uuid), timeout=20)


def test_tun_name_clash(active):
    first = active("pnv-c-clash1", uuid="5ca1ab1e-0000-4000-8000-000000000001")
    profile("pnv-c-clash2", password="pw", uuid="5ca1ab1e-0000-4000-8000-000000000002")
    since = cursor()
    try:
        r = connect("pnv-c-clash2")
        assert r.returncode != 0
        assert f"the interface name {tun(first)} is in use by the active profile {first}" in journal(since)
        assert units(first).get(f"plasma-nm-vpn-core-{first}.service") == "active"
        assert sh(f"cat /sys/class/net/{tun(first)}/carrier").strip() == "1"
    finally:
        sh("nmcli connection delete pnv-c-clash2")
