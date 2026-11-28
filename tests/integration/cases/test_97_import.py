# Import in the editors (6.7) without a desktop, through the test driver (editors.py): the bench's links pasted
# into the form and its AmneziaWG .conf opened as a file, then connected; the messages for what is dropped or
# refused. The confirmation before replacing a filled-in form is a unit test (it is a dialog).
import os
import re

import pytest

from conftest import sh
from editors import form, form_available, show, up, works  # noqa: F401
from helpers import wait_for
from test_32_network import bench_secrets
from test_40_awg import CLIENT, handshake
from test_60_killswitch import names  # noqa: F401

pytestmark = [pytest.mark.test_build, pytest.mark.usefixtures("form_available")]

HY2 = "org.freedesktop.NetworkManager.pnv.hysteria2"
VLESS = "org.freedesktop.NetworkManager.pnv.vless"
AWG = "org.freedesktop.NetworkManager.pnv.amneziawg"


def hy2_link():
    s = bench_secrets()
    return (f"hysteria2://{s['HY2_PASSWORD']}@srv.stand.test:443/?obfs=salamander&obfs-password={s['HY2_OBFS']}"
            "&sni=srv.stand.test&insecure=1&alpn=h3#bench")


def vless_link():
    s = bench_secrets()
    path = s["VLESS_PATH"].replace("/", "%2F")
    return (f"vless://{s['VLESS_UUID']}@198.51.100.1:443?type=xhttp&security=reality&encryption=none"
            f"&pbk={s['REALITY_PUBLIC']}&sid={s['VLESS_SHORT_ID']}&sni=www.stand.test&path={path}&fp=chrome#bench")


def imported(service, name, how, *after):
    """A new form, the import, then fields set by hand (DNS, storage) and saved."""
    return form(service, "new", name, *how, *after)


@pytest.mark.parametrize("protocol", ["hysteria2", "vless"])
def test_pasted_link_connects(names, protocol):
    names.append("pnv-i-link")
    service, link = (HY2, hy2_link()) if protocol == "hysteria2" else (VLESS, vless_link())
    secrets = ["password", "obfs-password"] if protocol == "hysteria2" else ["uuid"]
    # The import keeps the fields' default storage (the user's keychain), which needs an agent: stored for all here.
    out = imported(service, "pnv-i-link", [f"importText={link}", "click=importButton"], "dns=203.0.113.1",
                   *(f"{k}:option=all" for k in secrets))
    assert out["valid"] and "error" not in out, out
    assert "Imported" in out.get("import", ""), out
    assert set(show("pnv-i-link", "vpn.secrets")) == set(secrets)
    assert up("pnv-i-link").returncode == 0
    assert "203.0.113.1" in sh("dig +short +tries=2 +time=3 @203.0.113.1 stand.test A")
    works("pnv-i-link")


def test_failed_import_saves_nothing(names):
    names.append("pnv-i-bad")
    out = imported(HY2, "pnv-i-bad", ["importText=hy2://x@h.example:443?obfs=gecko", "click=importButton"])
    assert not out["valid"] and "gecko is not supported" in out.get("import", ""), out
    assert sh("nmcli -g NAME connection show", check=False).count("pnv-i-bad") == 0


@pytest.fixture
def conf(tmp_path):
    def write(text):
        path = tmp_path / "import.conf"
        path.write_text(text)
        os.chmod(tmp_path, 0o755)
        return str(path)
    return write


def test_conf_file_connects_with_default_dns(names, conf):
    """The bench's client .conf has no DNS: the field stays empty and the daemon gives the defaults. It has no
    Address either (the AmneziaWG tests set it): added here, as a provider's .conf would have it."""
    names.append("pnv-i-conf")
    text = open(CLIENT).read().replace("[Interface]", "[Interface]\nAddress = 100.64.0.2/24", 1)
    assert not re.search(r"^\s*DNS\s*=", text, re.M | re.I)
    out = imported(AWG, "pnv-i-conf", [f"importFile={conf(text)}"], "private-key:option=all",
                   "awg-headerprotectionkey:option=all", "allowedIps=0.0.0.0/0")
    assert out["valid"] and "error" not in out, out
    uuid = out["uuid"]
    assert "dns" not in show("pnv-i-conf", "vpn.data")
    assert up("pnv-i-conf").returncode == 0
    assert wait_for(lambda: handshake(uuid) > 0, timeout=20)
    dns = sh("nmcli -g IP4.DNS connection show pnv-i-conf")
    assert "1.1.1.1" in dns and "9.9.9.9" in dns, dns
    sh("nmcli connection down pnv-i-conf")


def test_conf_warnings_are_listed(names, conf):
    names.append("pnv-i-warn")
    text = open(CLIENT).read().replace("[Interface]", "[Interface]\nPostUp = touch /tmp/x\nTable = off\n"
                                       "Address = fd00::2/64, 10.8.0.2/24, 10.8.0.3/24", 1)
    out = imported(AWG, "pnv-i-warn", [f"importFile={conf(text)}"])
    message = out.get("import", "")
    for dropped in ("PostUp is a wg-quick setting", "Table is a wg-quick setting", "Address: entry 1 is IPv6",
                    "Address: entry 3 was dropped"):
        assert dropped in message, message


@pytest.mark.parametrize("case", ["peers", "gecko", "other-protocol"])
def test_refused_imports_leave_the_form(names, conf, case):
    names.append("pnv-i-ref")
    if case == "peers":
        text = open(CLIENT).read()
        peer = text[text.index("[Peer]"):]
        out = imported(AWG, "pnv-i-ref", [f"importFile={conf(text + chr(10) + peer)}"])
        expected = "more than one [Peer]"
    elif case == "gecko":
        out = imported(HY2, "pnv-i-ref", ["importText=hy2://x@h.example:443?obfs=gecko", "click=importButton"])
        expected = "gecko is not supported"
    else:
        out = imported(HY2, "pnv-i-ref", [f"importText={vless_link()}", "click=importButton"])
        expected = "VLESS XHTTP Reality"
    assert expected in out.get("import", ""), out
    assert not out["valid"]
    assert sh("nmcli -g NAME connection show", check=False).count("pnv-i-ref") == 0
