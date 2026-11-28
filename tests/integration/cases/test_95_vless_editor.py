# The VLESS XHTTP Reality editor (6.4) without a desktop, through the test driver (editors.py).
import os

import pytest

from conftest import sh
from editors import form, form_available, persistent, show, up, works  # noqa: F401
from helpers import cursor, wait_for
from test_32_network import bench_secrets
from test_60_killswitch import daemon_of, kill_switch, names, succeeded, units  # noqa: F401

pytestmark = [pytest.mark.test_build, pytest.mark.usefixtures("form_available")]

SERVICE = "org.freedesktop.NetworkManager.pnv.vless"
FINGERPRINTS = "/usr/share/plasma-nm-vpn/xray/tls-fingerprint.list"


def new(name, **fields):
    s = bench_secrets()
    values = {"server": "198.51.100.1", "port": "443", "sni": "www.stand.test", "pbk": s["REALITY_PUBLIC"],
              "sid": s["VLESS_SHORT_ID"], "path": s["VLESS_PATH"], "uuid": s["VLESS_UUID"], "uuid:option": "all",
              "dns": "203.0.113.1", **fields}
    return form(SERVICE, "new", name, *(f"{k}={v}" for k, v in values.items() if v is not None))


def test_profile_from_the_form_connects(names):
    names.append("pnv-v-form")
    out = new("pnv-v-form")
    assert out["valid"] and "error" not in out, out
    data, secrets = show("pnv-v-form", "vpn.data"), show("pnv-v-form", "vpn.secrets")
    assert set(secrets) == {"uuid"} and "uuid" not in data and data["uuid-flags"] == "0"
    assert data["encryption"] == "none" and data["schema"] == "1"
    assert persistent("pnv-v-form") == "yes"
    assert up("pnv-v-form").returncode == 0
    assert "203.0.113.1" in sh("dig +short +tries=2 +time=3 @203.0.113.1 stand.test A")
    works("pnv-v-form")


def test_asked_every_time(names, tmp_path):
    names.append("pnv-v-ask")
    new("pnv-v-ask", **{"uuid:option": "ask"})
    assert show("pnv-v-ask", "vpn.data")["uuid-flags"] == "2" and "uuid" not in show("pnv-v-ask", "vpn.secrets")
    passwd = tmp_path / "passwd"
    passwd.write_text(f"vpn.secrets.uuid:{bench_secrets()['VLESS_UUID']}\n")
    assert up("pnv-v-ask", "passwd-file", str(passwd)).returncode == 0
    works("pnv-v-ask")


def test_resave_keeps_mlkem_and_persistent(names):
    """An ML-KEM encryption written outside the editor survives a save through it, byte for byte."""
    names.append("pnv-v-mlkem")
    uuid = new("pnv-v-mlkem")["uuid"]
    golden = open(f"{os.path.dirname(__file__)}/../../golden/xray-26.11.9/mlkem.json").read()
    start = golden.index("mlkem768x25519plus.")
    value = golden[start:golden.index('"', start)]
    sh(f"nmcli connection modify pnv-v-mlkem +vpn.data encryption={value} vpn.persistent no")
    out = form(SERVICE, "edit", uuid, "mtu=1400")
    assert out["valid"] and "error" not in out, out
    data = show("pnv-v-mlkem", "vpn.data")
    assert data["encryption"] == value and data["mtu"] == "1400"
    assert persistent("pnv-v-mlkem") == "yes" and "uuid" in show("pnv-v-mlkem", "vpn.secrets")
    assert not form(SERVICE, "edit", uuid, "encryption=mlkem768x25519plus.a/b")["valid"]


def test_choices_come_from_the_data_file(names):
    names.append("pnv-v-fp")
    assert "cannot set fingerprint" in new("pnv-v-fp", fingerprint="pnv-test-fp").get("driver", "")
    sh(f"cp {FINGERPRINTS} /tmp/pnv-fp.list && echo pnv-test-fp >> {FINGERPRINTS}")
    try:
        out = new("pnv-v-fp", fingerprint="pnv-test-fp")
        assert out["valid"] and show("pnv-v-fp", "vpn.data")["tls-fingerprint"] == "pnv-test-fp"
    finally:
        sh(f"cp /tmp/pnv-fp.list {FINGERPRINTS}")


@pytest.mark.parametrize("field", ['xhttpExtra={"noSuchKey": 1}', 'extra={"streamSettings": {"sockopt": {"path": "/x"}}}',
                                   "pbk=not+base64url", "uuid=not-a-uuid"])
def test_invalid_form_saves_nothing(names, field):
    names.append("pnv-v-bad")
    out = new("pnv-v-bad", **dict([field.split("=", 1)]))
    assert not out["valid"] and out["problems"], out
    assert sh("nmcli -g NAME connection show", check=False).count("pnv-v-bad") == 0


def test_banner_in_the_form(names):
    names.append("pnv-v-block")
    uuid = new("pnv-v-block", killSwitch="yes")["uuid"]
    since = cursor()
    assert up("pnv-v-block").returncode == 0 and succeeded(since, uuid)
    sh(f"kill -9 {daemon_of(uuid)}")
    assert wait_for(lambda: not units(uuid), timeout=20) and kill_switch(uuid)
    out = form(SERVICE, "edit", uuid)
    assert "Kill switch block active" in out.get("banner", "") and "pnv-v-block" in out["banner"], out
