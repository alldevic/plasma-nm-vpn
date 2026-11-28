# The AmneziaWG editor (6.5) without a desktop, through the test driver (editors.py).
import re

import pytest

from conftest import sh
from editors import form, form_available, persistent, show, up, works  # noqa: F401
from helpers import cursor, wait_for
from test_40_awg import CLIENT, PARAMS, handshake
from test_60_killswitch import daemon_of, kill_switch, names, succeeded, units  # noqa: F401

pytestmark = [pytest.mark.test_build, pytest.mark.usefixtures("form_available")]

SERVICE = "org.freedesktop.NetworkManager.pnv.amneziawg"
ALLOWLIST = "/usr/share/plasma-nm-vpn/amneziawg/amneziawg-params.list"


def client():
    return dict(re.findall(r"^(\w+)\s*=\s*(.+?)\s*$", open(CLIENT).read(), re.M))


def new(name, params=None, **fields):
    """A profile from the bench's client config, typed into the form; params replace the table's rows."""
    kv = client()
    host, port = kv["Endpoint"].rsplit(":", 1)
    values = {"server": host, "port": port, "address": "100.64.0.2/24", "peerPublicKey": kv["PublicKey"],
              "allowedIps": "0.0.0.0/0", "keepalive": kv["PersistentKeepalive"], "dns": "203.0.113.1",
              "private-key": kv["PrivateKey"], "private-key:option": "all",
              "awg-headerprotectionkey": kv["HeaderProtectionKey"], "awg-headerprotectionkey:option": "all",
              **fields}
    args = [f"{k}={v}" for k, v in values.items() if v is not None]
    rows = params if params is not None else {f"awg-{k.lower()}": kv[k] for k in PARAMS if k in kv}
    for i, (key, value) in enumerate(rows.items()):
        args += ["click=addParam", f"paramKey{i}={key}", f"paramValue{i}={value}"]
    return form(SERVICE, "new", name, *args)


def test_profile_from_the_form_connects(names):
    names.append("pnv-a-form")
    out = new("pnv-a-form")
    assert out["valid"] and "error" not in out, out
    uuid = out["uuid"]
    data, secrets = show("pnv-a-form", "vpn.data"), show("pnv-a-form", "vpn.secrets")
    assert set(secrets) == {"private-key", "awg-headerprotectionkey"}
    assert not {"private-key", "preshared-key", "awg-headerprotectionkey"} & set(data)
    assert data["preshared-key-flags"] == "4" and data["awg-jc"] == client()["Jc"]
    assert persistent("pnv-a-form") == "yes"
    assert up("pnv-a-form").returncode == 0
    assert wait_for(lambda: handshake(uuid) > 0, timeout=20)
    works("pnv-a-form")


def test_without_optional_keys_no_prompt(names):
    """Both optional keys left empty are not required; the bench needs the header protection key, so the
    connection itself is not expected to work, only to start without asking."""
    names.append("pnv-a-plain")
    new("pnv-a-plain", **{"awg-headerprotectionkey": "", "awg-headerprotectionkey:option": "none"})
    data = show("pnv-a-plain", "vpn.data")
    assert data["preshared-key-flags"] == "4" and data["awg-headerprotectionkey-flags"] == "4"
    since = cursor()
    up("pnv-a-plain")
    assert "is missing, asking NM" not in sh(f"journalctl --after-cursor '{since}' --no-pager -o cat")


def test_split_default_route_is_saved_whole(names):
    names.append("pnv-a-split")
    out = new("pnv-a-split", allowedIps="0.0.0.0/1, 128.0.0.0/1", killSwitch="yes")
    assert out["valid"], out
    data = show("pnv-a-split", "vpn.data")
    assert data["allowed-ips"] == "0.0.0.0/0" and data["kill-switch"] == "yes"


def test_key_added_to_the_allowlist_is_offered(names):
    names.append("pnv-a-list")
    assert "cannot set paramKey0" in new("pnv-a-list", params={"awg-pnvtest": "1"}).get("driver", "")
    sh(f"cp {ALLOWLIST} /tmp/pnv-awg.list && echo 'awg-pnvtest Jc uint16' >> {ALLOWLIST}")
    try:
        out = new("pnv-a-list", params={"awg-pnvtest": "1"})
        assert out["valid"] and show("pnv-a-list", "vpn.data")["awg-pnvtest"] == "1"
    finally:
        sh(f"cp /tmp/pnv-awg.list {ALLOWLIST}")


@pytest.mark.parametrize("change", [{"params": {"awg-jc": "70000"}}, {"params": {"awg-randomtrailers": "yes"}},
                                    {"address": "100.64.0.2/24, 100.64.0.3/24"}, {"address": "fd00::2/64"}])
def test_invalid_form_saves_nothing(names, change):
    names.append("pnv-a-bad")
    out = new("pnv-a-bad", **change)
    assert not out["valid"] and out["problems"], out
    assert sh("nmcli -g NAME connection show", check=False).count("pnv-a-bad") == 0


def test_resave_keeps_persistent(names):
    names.append("pnv-a-save")
    uuid = new("pnv-a-save")["uuid"]
    sh("nmcli connection modify pnv-a-save vpn.persistent no")
    out = form(SERVICE, "edit", uuid, "mtu=1400")
    assert out["valid"] and "error" not in out, out
    assert persistent("pnv-a-save") == "yes" and show("pnv-a-save", "vpn.data")["mtu"] == "1400"
    assert set(show("pnv-a-save", "vpn.secrets")) == {"private-key", "awg-headerprotectionkey"}


def test_banner_in_the_form(names):
    names.append("pnv-a-block")
    uuid = new("pnv-a-block", killSwitch="yes")["uuid"]
    since = cursor()
    assert up("pnv-a-block").returncode == 0 and succeeded(since, uuid)
    sh(f"kill -9 {daemon_of(uuid)}")
    assert wait_for(lambda: not units(uuid), timeout=20) and kill_switch(uuid)
    out = form(SERVICE, "edit", uuid)
    assert "Kill switch block active" in out.get("banner", "") and "pnv-a-block" in out["banner"], out
