# The Hysteria2 editor (6.3) without a desktop, through the test driver (editors.py).
import pytest

from conftest import sh
from editors import form, form_available, persistent, show, up, works  # noqa: F401
from helpers import cursor, wait_for
from test_32_network import bench_secrets
from test_60_killswitch import daemon_of, kill_switch, names, succeeded, units  # noqa: F401

pytestmark = [pytest.mark.test_build, pytest.mark.usefixtures("form_available")]

SERVICE = "org.freedesktop.NetworkManager.pnv.hysteria2"


def new(name, **fields):
    s = bench_secrets()
    values = {"server": "srv.stand.test", "port": "443", "alpn": "h3", "insecure": "yes", "obfs": "salamander",
              "password": s["HY2_PASSWORD"], "obfs-password": s["HY2_OBFS"], "password:option": "all",
              "obfs-password:option": "all", "dns": "203.0.113.1", **fields}
    return form(SERVICE, "new", name, *(f"{k}={v}" for k, v in values.items() if v is not None))


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
    out = form(SERVICE, "edit", uuid, "mtu=1400")
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
    new("pnv-e-plain", port="4443", obfs="", **{"obfs-password": "", "obfs-password:option": "none"})
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
    out = form(SERVICE, "edit", uuid)
    assert "Kill switch block active" in out.get("banner", "") and "pnv-e-block" in out["banner"], out
