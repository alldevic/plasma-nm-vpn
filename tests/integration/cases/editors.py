# The editors without a desktop (6.3–6.5): the test driver (tests/integration/tools/editor-form.cpp) loads the
# installed editor plugin as plasma-nm does, fills its fields by object name and saves through ConnectionSettings
# as plasma-nm's connection editor does. The applet, kded6's prompt and the user's keychain are checked by hand on
# the desktop (milestone records).
import configparser
import glob
import json
import os
import subprocess

import pytest

from conftest import TEST_BUILD_DIR, sh
from helpers import fetch


def driver():
    found = [os.environ.get("PNV_EDITOR_FORM", ""),
             *glob.glob(f"{TEST_BUILD_DIR or '/nonexistent'}/usr/lib/*/plasma-nm-vpn/test/pnv-editor-form"),
             *glob.glob("/usr/lib/*/plasma-nm-vpn/test/pnv-editor-form")]
    return next((f for f in found if f and os.path.exists(f)), None)


@pytest.fixture(scope="module")
def form_available():
    if not driver():
        pytest.skip("no editor test driver (test build with the editors)")


def form(service, *args):
    r = subprocess.run([driver(), service, *args], capture_output=True, text=True,
                       env={**os.environ, "QT_QPA_PLATFORM": "offscreen"})
    lines = [line for line in r.stdout.splitlines() if line.startswith("{")]
    # No result: the driver could not fill a field (e.g. a value the choice does not offer).
    return json.loads(lines[-1]) if lines else {"valid": False, "driver": r.stderr}


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
