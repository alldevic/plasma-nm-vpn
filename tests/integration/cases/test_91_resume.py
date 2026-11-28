# The reconnect cycle after a real suspend (5.5): the tunnel comes back without anyone touching it.
# Runs after test_90 for the same reason: the VM sleeps, the harness reconnects over vsock.
import time

import pytest

from conftest import sh
from helpers import cursor, fetch, journal, wait_for
from test_32_network import connect, hy2

pytestmark = [pytest.mark.kvm, pytest.mark.test_build]


def test_tunnel_back_after_suspend():
    name = "pnv-s-resume"
    uuid = hy2(name, server="198.51.100.1")
    try:
        since = cursor()
        assert connect(name).returncode == 0
        # A resume before the first liveness check has passed is no trigger: the session has not succeeded yet.
        assert wait_for(lambda: f"[{uuid}] session succeeded" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn"),
                        timeout=30)
        assert "pnv stand: internet" in fetch("http://203.0.113.1/")
        since = cursor()
        sh("rtcwake -m no -s 20")
        sh("systemctl suspend")
        assert wait_for(lambda: "reconnected (trigger: resume" in journal(since, "GLIB_DOMAIN=plasma-nm-vpn"),
                        timeout=150), journal(since, "GLIB_DOMAIN=plasma-nm-vpn")
        assert sh(f"nmcli -g GENERAL.STATE connection show {name}").strip() == "activated"
        assert "pnv stand: internet" in fetch("http://203.0.113.1/")
        assert f"[{uuid}] giving up" not in journal(since, "GLIB_DOMAIN=plasma-nm-vpn")
    finally:
        sh(f"nmcli connection down {name}", check=False)
        sh(f"nmcli connection delete {name}", check=False)
        time.sleep(1)
