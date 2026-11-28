# The Hysteria2 editor (6.3) without a desktop: profiles made by its form through the test driver
# (tests/integration/tools/editor-form.cpp), which loads the installed plugin as plasma-nm does and saves through
# ConnectionSettings as plasma-nm's connection editor does; then connected with nmcli. The applet, kded6's prompt
# and the user's keychain are checked by hand on the desktop (milestone record).
import configparser
import glob
import json
import os
import subprocess

import pytest

from conftest import TEST_BUILD_DIR, sh
from helpers import cursor, fetch, wait_for
from test_32_network import bench_secrets
from test_60_killswitch import daemon_of, kill_switch, names, succeeded, units  # noqa: F401

pytestmark = pytest.mark.test_build

SERVICE = "org.freedesktop.NetworkManager.pnv.hysteria2"


def driver():
    found = [os.environ.get("PNV_EDITOR_FORM", ""),
             *glob.glob(f"{TEST_BUILD_DIR or '/nonexistent'}/usr/lib/*/plasma-nm-vpn/test/pnv-editor-form"),
             *glob.glob("/usr/lib/*/plasma-nm-vpn/test/pnv-editor-form")]
    return next((f for f in found if f and os.path.exists(f)), None)


@pytest.fixture(scope="module", autouse=True)
def form_available():
    if not driver():
        pytest.skip("no editor test driver (test build with the editors)")


def form(*args):
    r = subprocess.run([driver(), SERVICE, *args], capture_output=True, text=True,
                       env={**os.environ, "QT_QPA_PLATFORM": "offscreen"})
    lines = [line for line in r.stdout.splitlines() if line.startswith("{")]
    assert lines, r.stderr
    return json.loads(lines[-1])


def new(name, **fields):
    s = bench_secrets()
    values = {"server": "srv.stand.test", "port": "443", "alpn": "h3", "insecure": "yes", "obfs": "salamander",
              "password": s["HY2_PASSWORD"], "obfsPassword": s["HY2_OBFS"], "password:option": "all",
              "obfsPassword:option": "all", "dns": "203.0.113.1", **fields}
    return form("new", name, *(f"{k}={v}" for k, v in values.items() if v is not None))


def show(name, field):
    """vpn.data or vpn.secrets as NM saved them: the [vpn] and [vpn-secrets] groups of the profile's keyfile."""
    keyfile = configparser.RawConfigParser(interpolation=None)
    keyfile.optionxform = str
    uuid = sh(f"nmcli -g connection.uuid connection show {name}").strip()
    keyfile.read(sh(f"grep -l '^uuid={uuid}$' /etc/NetworkManager/system-connections/*").split()[0])
    group = {"vpn.data": "vpn", "vpn.secrets": "vpn-secrets"}[field]
    items = dict(keyfile[group]) if keyfile.has_section(group) else {}
    return {k: v for k, v in items.items() if k not in ("service-type", "persistent")}


def persistent(name):
    return sh(f"nmcli -g vpn.persistent connection show {name}").strip()


def up(name, *extra):
    return subprocess.run(["nmcli", "--wait", "40", "connection", "up", name, *extra], capture_output=True, text=True)


def works(name):
    assert "pnv stand: internet" in fetch("http://203.0.113.1/")
    sh(f"nmcli connection down {name}")


@pytest.mark.parametrize("ports", ["443", "443,20000-20100"])
def test_profile_from_the_form_connects(names, ports):
    names.append("pnv-e-hy2")
    out = new("pnv-e-hy2", port=ports)
    assert out["valid"] and "error" not in out, out
    data, secrets = show("pnv-e-hy2", "vpn.data"), show("pnv-e-hy2", "vpn.secrets")
    assert data["ports"] == ports and "port" not in data
    assert data["password-flags"] == "0" and data["obfs-password-flags"] == "0" and data["schema"] == "1"
    assert "password" not in data and "obfs-password" not in data
    assert set(secrets) == {"password", "obfs-password"}
    assert persistent("pnv-e-hy2") == "yes"
    assert up("pnv-e-hy2").returncode == 0
    works("pnv-e-hy2")


def test_resave_keeps_persistent_and_secrets(names):
    names.append("pnv-e-save")
    uuid = new("pnv-e-save")["uuid"]
    sh("nmcli connection modify pnv-e-save vpn.persistent no")
    out = form("edit", uuid, "mtu=1400")
    assert out["valid"] and "error" not in out, out
    assert persistent("pnv-e-save") == "yes"
    assert show("pnv-e-save", "vpn.data")["mtu"] == "1400" and "password" in show("pnv-e-save", "vpn.secrets")
    assert up("pnv-e-save").returncode == 0
    works("pnv-e-save")


def test_asked_every_time(names, tmp_path):
    names.append("pnv-e-ask")
    new("pnv-e-ask", **{"password:option": "ask"})
    assert show("pnv-e-ask", "vpn.data")["password-flags"] == "2"
    assert "password" not in show("pnv-e-ask", "vpn.secrets")
    assert up("pnv-e-ask").returncode != 0
    passwd = tmp_path / "passwd"
    passwd.write_text(f"vpn.secrets.password:{bench_secrets()['HY2_PASSWORD']}\n")
    assert up("pnv-e-ask", "passwd-file", str(passwd)).returncode == 0
    works("pnv-e-ask")


def test_without_salamander_needs_no_obfuscation_password(names):
    """The bench's Hysteria2 inbound on 4443 has no obfuscation."""
    names.append("pnv-e-plain")
    new("pnv-e-plain", port="4443", obfs="", obfsPassword="", **{"obfsPassword:option": "none"})
    data = show("pnv-e-plain", "vpn.data")
    assert "obfs" not in data and data["obfs-password-flags"] == "4"
    assert "obfs-password" not in show("pnv-e-plain", "vpn.secrets")
    assert up("pnv-e-plain").returncode == 0
    works("pnv-e-plain")


@pytest.mark.parametrize("field", ["sni=srv.stand.test\nx", 'extra={"no_such_key": 1}', 'extra={"detour": "x"}'])
def test_invalid_form_saves_nothing(names, field):
    names.append("pnv-e-bad")
    out = new("pnv-e-bad", **dict([field.split("=", 1)]))
    assert not out["valid"] and out["problems"], out
    assert not any("bad-" in p or "-not-allowed" in p for p in out["problems"])
    assert sh("nmcli -g NAME connection show", check=False).count("pnv-e-bad") == 0


def test_banner_in_the_form(names):
    names.append("pnv-e-block")
    uuid = new("pnv-e-block", killSwitch="yes")["uuid"]
    since = cursor()
    assert up("pnv-e-block").returncode == 0 and succeeded(since, uuid)
    sh(f"kill -9 {daemon_of(uuid)}")
    assert wait_for(lambda: not units(uuid), timeout=20) and kill_switch(uuid)
    out = form("edit", uuid)
    assert "Kill switch block active" in out.get("banner", "") and "pnv-e-block" in out["banner"], out
