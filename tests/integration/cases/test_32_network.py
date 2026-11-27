# Network parameters (2.4): the persistent TUN, its /30, the route to the server, DNS, the server IP cache.
import json
import os
import subprocess
import time

import pytest

from conftest import DNS_MODE, sh
from helpers import cursor, gone, journal, ok, tun, units, uuid_of, wait_for

pytestmark = pytest.mark.test_build

SECRETS = os.path.join(os.path.dirname(__file__), "..", "stand", "secrets", "env")
BASE = "ports=443, obfs=salamander, tls-alpn=h3, tls-insecure=yes, password-flags=0, obfs-password-flags=0"


def bench_secrets():
    pairs = (line.split("=", 1) for line in open(SECRETS).read().splitlines() if "=" in line)
    return {k: v.strip().strip("'\"") for k, v in pairs}


def hy2(name, server="srv.stand.test", data="", never_default=False):
    s = bench_secrets()
    sh(f"nmcli connection delete {name}", check=False)
    extra = " ipv4.never-default yes" if never_default else ""
    sh(f"nmcli connection add type vpn con-name {name} autoconnect no "
       f"vpn-type org.freedesktop.NetworkManager.pnv.hysteria2 vpn.data 'server={server}, {BASE}{data}' "
       f"vpn.secrets 'password={s['HY2_PASSWORD']}, obfs-password={s['HY2_OBFS']}'{extra}")
    return uuid_of(name)


def connect(name, wait=30):
    return subprocess.run(["nmcli", "--wait", str(wait), "connection", "up", name], capture_output=True, text=True)


def outbound(uuid):
    return json.loads(sh(f"cat /run/plasma-nm-vpn/{uuid}/config.json"))["outbounds"][0]


def subnet(uuid):
    out = sh(f"ip -4 -o addr show dev {tun(uuid)}").split()
    return out[out.index("inet") + 1]


@pytest.fixture
def names():
    made = []
    yield made
    for name in made:
        sh(f"nmcli connection down {name}", check=False)
        sh(f"nmcli connection delete {name}", check=False)


def test_tun_route_and_cache(names):
    names.append("pnv-n-basic")
    uuid = hy2("pnv-n-basic")
    sh(f"rm -f /var/lib/plasma-nm-vpn/{uuid}")
    assert connect("pnv-n-basic").returncode == 0
    t = tun(uuid)
    assert "persist on" in sh(f"ip -d link show {t}")
    assert sh(f"cat /sys/class/net/{t}/owner").strip() == sh("id -u plasma-nm-vpn").strip()
    addr = subnet(uuid)
    assert addr.endswith("/30") and addr.startswith("198.1")
    assert "UP" in sh(f"ip -br link show {t}")
    route = sh("ip route get 198.51.100.1")
    assert "dev stand-cli" in route and t not in route
    assert outbound(uuid)["server"] == "198.51.100.1"
    assert outbound(uuid)["bind_interface"] == "stand-cli"
    assert wait_for(lambda: ok(f"test -f /var/lib/plasma-nm-vpn/{uuid}"), timeout=10)
    cache = sh(f"cat /var/lib/plasma-nm-vpn/{uuid}")
    assert "name=srv.stand.test" in cache and "ip=198.51.100.1" in cache
    assert "pnv stand: internet" in sh("curl -s -m 5 http://stand.test/")
    sh("nmcli connection down pnv-n-basic")
    assert wait_for(lambda: gone(uuid), timeout=20)


def test_default_dns(names):
    names.append("pnv-n-dns")
    uuid = hy2("pnv-n-dns")
    assert connect("pnv-n-dns").returncode == 0
    t = tun(uuid)
    assert sh(f"nmcli -g IP4.DNS device show {t}").strip() == "1.1.1.1 | 9.9.9.9"
    if DNS_MODE == "resolved":
        status = sh(f"resolvectl status {t}")
        assert "1.1.1.1" in status and "9.9.9.9" in status and "~." in status
    else:
        servers = [l.split()[1] for l in sh("cat /etc/resolv.conf").splitlines() if l.startswith("nameserver")]
        assert servers[:2] == ["1.1.1.1", "9.9.9.9"]


def test_never_default(names):
    names += ["pnv-n-nd", "pnv-n-nd-dns"]
    uuid = hy2("pnv-n-nd", never_default=True)
    assert connect("pnv-n-nd").returncode == 0
    t = tun(uuid)
    assert f"dev {t}" not in sh("ip route show default")
    assert "dev stand-cli" in sh("ip route get 198.51.100.1")
    assert sh(f"nmcli -g IP4.DNS device show {t}").strip() == ""
    sh("nmcli connection down pnv-n-nd")
    uuid2 = hy2("pnv-n-nd-dns", data=", dns=203.0.113.1", never_default=True)
    assert connect("pnv-n-nd-dns").returncode == 0
    t2 = tun(uuid2)
    assert sh(f"nmcli -g IP4.DNS device show {t2}").strip() == "203.0.113.1"
    assert f"dev {t2}" in sh("ip route get 203.0.113.1")


def test_subnet_avoids_addresses_in_use(names):
    names += ["pnv-n-pool", "pnv-n-pool2"]
    uuid = hy2("pnv-n-pool")
    assert connect("pnv-n-pool").returncode == 0
    first = subnet(uuid)
    sh("nmcli connection down pnv-n-pool")
    assert wait_for(lambda: gone(uuid), timeout=20)
    sh(f"ip link add pnvtest0 type dummy && ip addr add {first} dev pnvtest0")
    try:
        assert connect("pnv-n-pool").returncode == 0
        assert subnet(uuid) != first
    finally:
        sh("ip link del pnvtest0")
    sh("nmcli connection down pnv-n-pool")
    uuid2 = hy2("pnv-n-pool2")
    both = [subprocess.Popen(["nmcli", "--wait", "30", "connection", "up", n]) for n in ("pnv-n-pool", "pnv-n-pool2")]
    assert [p.wait(timeout=60) for p in both] == [0, 0]
    assert subnet(uuid) != subnet(uuid2)


def test_failed_attempt_leaves_the_cache_alone(names):
    names.append("pnv-n-broken")
    uuid = hy2("pnv-n-broken", data=", pnv-test-hook=broken-config")
    sh(f"rm -f /var/lib/plasma-nm-vpn/{uuid}")
    assert connect("pnv-n-broken").returncode != 0
    assert not ok(f"test -e /var/lib/plasma-nm-vpn/{uuid}")


@pytest.fixture
def no_dns():
    """The "ISP" DNS stops answering: queries to it are dropped."""
    sh("nft add table inet pnvtest && nft add chain inet pnvtest out '{ type filter hook output priority 0; }' && "
       "nft add rule inet pnvtest out udp dport 53 drop && nft add rule inet pnvtest out tcp dport 53 drop")
    yield
    sh("nft delete table inet pnvtest")


def test_unresolvable_server_without_cache(names, no_dns):
    names.append("pnv-n-nodns")
    uuid = hy2("pnv-n-nodns")
    sh(f"rm -f /var/lib/plasma-nm-vpn/{uuid}")
    since = cursor()
    start = time.monotonic()
    assert connect("pnv-n-nodns").returncode != 0
    assert time.monotonic() - start < 12
    assert "cannot resolve the server name" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn")
    assert f"plasma-nm-vpn-core-{uuid}.service" not in units(uuid)
    assert not ok(f"ip link show {tun(uuid)}") and not ok(f"nft list chain inet plasma_nm_vpn test-{uuid[:8]}")
    sh(f"printf '[server]\\nname=other.stand.test\\nip=198.51.100.1\\n' > /var/lib/plasma-nm-vpn/{uuid}")
    assert connect("pnv-n-nodns").returncode != 0
    sh(f"printf '[server]\\nname=srv.stand.test\\nip=198.51.100.1\\n' > /var/lib/plasma-nm-vpn/{uuid}")
    since = cursor()
    assert connect("pnv-n-nodns").returncode == 0
    assert "using the cached IP 198.51.100.1" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn")


def test_ip_literal_needs_no_dns(names):
    names.append("pnv-n-literal")
    hy2("pnv-n-literal", server="198.51.100.1")
    cap = subprocess.Popen(["timeout", "20", "tcpdump", "-n", "-l", "-i", "stand-cli", "port", "53"],
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    time.sleep(1)
    assert connect("pnv-n-literal").returncode == 0
    time.sleep(1)
    cap.terminate()
    assert cap.communicate()[0].strip() == ""


def test_cache_ages_by_modification_time():
    sh("touch /var/lib/plasma-nm-vpn/pnvtest-old /var/lib/plasma-nm-vpn/pnvtest-new")
    sh("touch -m -d '-91 days' /var/lib/plasma-nm-vpn/pnvtest-old")
    sh("systemd-tmpfiles --clean plasma-nm-vpn-common.conf")
    assert not ok("test -e /var/lib/plasma-nm-vpn/pnvtest-old")
    assert ok("test -e /var/lib/plasma-nm-vpn/pnvtest-new")
    sh("rm -f /var/lib/plasma-nm-vpn/pnvtest-new")


def hop_packets():
    out = sh("ip netns exec stand nft list chain inet stand hop")
    return int(out.split("counter packets ")[1].split()[0])


def test_port_hopping(names):
    names.append("pnv-n-hop")
    uuid = hy2("pnv-n-hop", server="198.51.100.1")
    sh("nmcli connection modify pnv-n-hop +vpn.data 'ports=20000-20100, hop-interval=5'")
    before = hop_packets()
    assert connect("pnv-n-hop").returncode == 0
    assert outbound(uuid)["server_ports"] == ["20000:20100"] and "server_port" not in outbound(uuid)
    assert "pnv stand: internet" in sh("curl -s -m 5 http://stand.test/")
    assert hop_packets() > before
