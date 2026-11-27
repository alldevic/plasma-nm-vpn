# The 0.7 prototype brings up the tunnel via nmcli (temporary payload, removed in 2.7).
import pathlib
import time

import pytest

from conftest import sh

SMOKE = pathlib.Path(__file__).resolve().parent.parent / "smoke"


@pytest.fixture
def prototype():
    sh(str(SMOKE / "install"), timeout=180)
    yield
    sh("nmcli connection down proto-hy2", check=False)
    sh("nmcli connection delete proto-hy2", check=False)


def test_prototype_brings_up_tunnel_via_nmcli(prototype):
    sh("nmcli --wait 30 connection up proto-hy2", timeout=60)
    # The system resolver may get the tunnel's DNS a moment after nmcli returns.
    for _ in range(20):
        if sh("getent hosts stand.test", check=False):
            break
        time.sleep(0.5)
    else:
        pytest.fail("stand.test does not resolve through the tunnel's DNS")
    assert sh("curl -s -m 10 http://stand.test/").strip() == "pnv stand: internet"
