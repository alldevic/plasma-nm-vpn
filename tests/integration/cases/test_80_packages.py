# Package removal (2.7): removing the core package brings its connections down and leaves no trace; removing
# plasma-nm-vpn-common leaves no table behind. The packages are installed again afterwards.
import glob

import pytest

from conftest import sh
from helpers import no_traces, units, wait_for
from test_32_network import connect, hy2

pytestmark = pytest.mark.packages

DEBS = "/root/debs"


@pytest.fixture
def reinstall():
    yield
    common, sb = glob.glob(f"{DEBS}/plasma-nm-vpn-common_*.deb"), glob.glob(f"{DEBS}/plasma-nm-vpn-sb_*.deb")
    sh(f"apt-get install -y -q {common[0]} {sb[0]}", timeout=300)
    sh("nmcli connection delete pnv-p-remove", check=False)


def test_remove_the_core_package_then_common(reinstall):
    uuid = hy2("pnv-p-remove")
    assert connect("pnv-p-remove").returncode == 0
    assert units(uuid)
    out = sh("apt-get remove -y -q plasma-nm-vpn-sb 2>&1", timeout=300)
    assert f"plasma-nm-vpn: brought down {uuid}, cleaned up after" in out
    assert sh("nmcli -g GENERAL.STATE connection show pnv-p-remove").strip() == ""
    assert no_traces(uuid) == []
    sh("nft add table inet plasma_nm_vpn && nft add chain inet plasma_nm_vpn tst-left")
    sh("touch /run/plasma-nm-vpn/guard-status")
    sh("apt-get remove -y -q plasma-nm-vpn-common", timeout=300)
    assert sh("nft list table inet plasma_nm_vpn 2>&1 || true").count("chain") == 0
    assert not wait_for(lambda: sh("test -e /run/plasma-nm-vpn/guard-status && echo yes || true").strip(), timeout=1)
