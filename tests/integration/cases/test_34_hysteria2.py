# The Hysteria2 suite (2.7): what the stage promises end to end, and that every failure leaves no trace.
import json
import subprocess
import time

import pytest

from conftest import sh
from helpers import core_pid, cursor, journal, no_traces, ok, units, wait_for, fetch
from test_32_network import connect, hy2

pytestmark = pytest.mark.test_build


@pytest.fixture
def names():
    made = []
    yield made
    for name in made:
        sh(f"nmcli connection down {name}", check=False)
        sh(f"nmcli connection delete {name}", check=False)


def config(uuid):
    return json.loads(sh(f"cat /run/plasma-nm-vpn/{uuid}/config.json"))


def test_connection_and_traffic(names):
    names.append("pnv-h-traffic")
    uuid = hy2("pnv-h-traffic")
    assert connect("pnv-h-traffic").returncode == 0
    assert sh("nmcli -g GENERAL.STATE connection show pnv-h-traffic").strip() == "activated"
    assert wait_for(lambda: "stand.test" in sh("getent hosts stand.test", check=False), timeout=10)
    assert "pnv stand: internet" in fetch("http://stand.test/")
    assert sh(f"ps -o user= -p {core_pid(uuid)}").strip() == "plasma-nm-vpn"
    sh("nmcli connection down pnv-h-traffic")
    assert no_traces(uuid) == []


def test_aaaa_only_server(names):
    names.append("pnv-h-v6")
    uuid = hy2("pnv-h-v6", server="srv6.stand.test")
    assert connect("pnv-h-v6").returncode == 0
    assert config(uuid)["outbounds"][0]["server"] == "2001:db8:100::1"
    assert "dev stand-cli" in sh("ip -6 route get 2001:db8:100::1")
    # Traffic flows, so the guard (which is installed) lets the core's own IPv6 to the server through.
    assert f"guard-out-{uuid}" in sh("nft list table inet plasma_nm_vpn")
    assert "pnv stand: internet" in sh("curl -4 -s -m 10 http://stand.test/")


def test_kill_9_of_the_core_before_success(names):
    names.append("pnv-h-corekill")
    uuid = hy2("pnv-h-corekill", data={"pnv-test-hook": "hold-before-config"})
    since = cursor()
    up = subprocess.Popen(["nmcli", "--wait", "60", "connection", "up", "pnv-h-corekill"], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True)
    assert wait_for(lambda: "holding Ip4Config" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn"), timeout=30)
    sh(f"kill -9 {core_pid(uuid)}")
    assert up.wait(timeout=60) != 0
    log = journal(since, "GLIB_DOMAIN=plasma-nm-vpn")
    assert f"giving up (connect failed): plasma-nm-vpn-core-{uuid}.service stopped" in log
    assert no_traces(uuid) == []
    sh("nmcli connection modify pnv-h-corekill -vpn.data pnv-test-hook")
    assert connect("pnv-h-corekill").returncode == 0


VECTORS = [
    ("vpn.data", "server=srv.stand.test\nx=1", "control-char"),
    ("vpn.data", "tls-sni=a.stand.test\r\nx", "control-char"),
    # nmcli does not store a secret with a newline: the keyfile carries it (\n is a newline there).
    ("keyfile", "password=a\\nb", "control-char"),
    ("vpn.data", 'extra={"detour":"direct"}', "extra-forbidden"),
    ("vpn.data", 'extra={"tls":{"certificate_path":"/etc/shadow"}}', "extra-forbidden"),
]


@pytest.mark.parametrize("prop,item,code", VECTORS)
def test_attack_vectors_via_nmcli(names, prop, item, code):
    names.append("pnv-h-attack")
    if prop == "keyfile":
        uuid = hy2("pnv-h-attack", secrets=dict([item.split("=", 1)]))
    else:
        uuid = hy2("pnv-h-attack")
        subprocess.run(["nmcli", "connection", "modify", "pnv-h-attack", f"+{prop}", item], check=True,
                       capture_output=True)
    since = cursor()
    assert connect("pnv-h-attack").returncode != 0
    log = journal(since, "GLIB_DOMAIN=plasma-nm-vpn")
    assert f"profile rejected ({code})" in log
    assert not units(uuid)
    assert "a\nb" not in log


def test_up_again_after_each_failure(names):
    names.append("pnv-h-again")
    uuid = hy2("pnv-h-again", data={"pnv-test-hook": "broken-config"})
    assert connect("pnv-h-again").returncode != 0
    assert no_traces(uuid) == []
    sh("nmcli connection modify pnv-h-again -vpn.data pnv-test-hook")
    assert connect("pnv-h-again").returncode == 0
    assert "pnv stand: internet" in fetch("http://stand.test/")
