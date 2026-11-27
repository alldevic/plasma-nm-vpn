# The daemon skeleton (2.1): bus name, secrets, unknown service types, giving up, synchronous disconnect.
import subprocess

import pytest

from conftest import sh
from helpers import PASSWORD, SERVICE, UNKNOWN, cursor, daemon_pid, journal, profile, up, wait_for

pytestmark = pytest.mark.test_build


@pytest.fixture
def since():
    return cursor()


@pytest.fixture
def passwd(tmp_path):
    f = tmp_path / "passwd"
    f.write_text(f"vpn.secrets.password:{PASSWORD}\n")
    return f


@pytest.fixture
def unknown_service():
    sh(f"sed -e 's/pnv-hysteria2/pnv-test-unknown/; s/{SERVICE}/{UNKNOWN}/' "
       "/usr/lib/NetworkManager/VPN/plasma-nm-vpn-hysteria2.name > /usr/lib/NetworkManager/VPN/pnv-test-unknown.name")
    sh(f"sed 's/{SERVICE}/{UNKNOWN}/g' /usr/share/dbus-1/system.d/plasma-nm-vpn-sb.conf "
       "> /usr/share/dbus-1/system.d/pnv-test-unknown.conf && systemctl reload dbus")
    yield
    sh("rm -f /usr/lib/NetworkManager/VPN/pnv-test-unknown.name /usr/share/dbus-1/system.d/pnv-test-unknown.conf")


def test_daemon_takes_its_bus_name(since, passwd):
    name = profile("pnv-t-bus")
    nmcli = up(name, "passwd-file", str(passwd))
    try:
        assert wait_for(lambda: f"{SERVICE}.Connection_" in sh("busctl --system list --no-pager", check=False))
        assert wait_for(lambda: "Connect: hysteria2" in journal(since), timeout=10)
    finally:
        sh(f"nmcli connection down {name}", check=False)
        nmcli.wait(timeout=90)


def test_missing_secret_is_requested_and_passwd_file_reaches_connect(since, passwd):
    name = profile("pnv-t-secret", flags=2)  # not saved: ask every time
    r = subprocess.run(["nmcli", "--wait", "15", "connection", "up", name], capture_output=True, text=True)
    assert r.returncode != 0
    log = journal(since)
    assert "secret password is missing" in log
    assert "Connect: hysteria2" not in log
    since2 = cursor()
    nmcli = up(name, "passwd-file", str(passwd))
    try:
        assert wait_for(lambda: "Connect: hysteria2" in journal(since2))
    finally:
        sh(f"nmcli connection down {name}", check=False)
        nmcli.wait(timeout=90)


def test_unknown_service_type_fails_without_launching(since, passwd, unknown_service):
    name = profile("pnv-t-unknown", service=UNKNOWN)
    r = subprocess.run(["nmcli", "--wait", "30", "connection", "up", name, "passwd-file", str(passwd)],
                       capture_output=True, text=True)
    assert r.returncode != 0
    log = journal(since)
    assert f"service type {UNKNOWN} is not handled" in log
    assert "Connect: " not in log


def test_give_up_fails_the_connection_with_a_reason(since, passwd):
    name = profile("pnv-t-giveup", data="server=198.51.100.1, ports=443, pnv-test-hook=give-up")
    r = subprocess.run(["nmcli", "--wait", "30", "connection", "up", name, "passwd-file", str(passwd)],
                       capture_output=True, text=True)
    assert r.returncode != 0
    log = journal(since)
    assert "giving up (connect failed): test hook: give up as requested" in log
    assert PASSWORD not in log


def test_disconnect_is_synchronous(since, passwd):
    name = profile("pnv-t-down")
    nmcli = up(name, "passwd-file", str(passwd))
    assert wait_for(lambda: daemon_pid())
    pid = daemon_pid()[0]
    sh(f"nmcli connection down {name}")
    nmcli.wait(timeout=90)
    assert wait_for(lambda: not sh(f"ps -p {pid} -o pid=", check=False).strip(), timeout=30)
    log = journal(since)
    assert log.index("test hook: slow cleanup step done") < log.index("Disconnect done") < log.rindex("] exit")
