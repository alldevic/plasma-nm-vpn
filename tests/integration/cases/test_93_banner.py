# The kill switch banner (6.2) on real blocks, through the test form (tests/integration/tools/banner-form.cpp)
# run offscreen as a regular user: a temporary polkit rule stands in for the administrator's consent, its
# absence for a refusal (no polkit agent answers).
import glob
import json
import os
import shutil
import subprocess
import time

import pytest

from conftest import TEST_BUILD_DIR, sh
from helpers import wait_for
from test_60_killswitch import entry, kill_switch, names  # noqa: F401
from test_70_blocks import N, block

pytestmark = pytest.mark.test_build

FORM = "/tmp/pnv-banner-form"
RULE = "/etc/polkit-1/rules.d/49-pnv-test.rules"
LIVENESS = "/run/pnv-test-liveness-timeout"


def form_binary():
    """The test build's form (CI), or one built by hand (PNV_BANNER_FORM, locally)."""
    found = os.environ.get("PNV_BANNER_FORM") or next(
        iter(glob.glob(f"{TEST_BUILD_DIR or '/nonexistent'}/usr/lib/*/plasma-nm-vpn/test/pnv-banner-form")), None)
    return found if found and os.path.exists(found) else None


@pytest.fixture(scope="module", autouse=True)
def user_and_form():
    binary = form_binary()
    if not binary:
        pytest.skip("no banner test form (test build with the editors)")
    shutil.copy(binary, FORM)
    os.chmod(FORM, 0o755)
    sh("id pnv-test >/dev/null 2>&1 || useradd --no-create-home pnv-test")
    yield
    sh(f"rm -f {FORM} {RULE}")


def consent(yes):
    if yes:
        with open(RULE, "w") as f:
            f.write('polkit.addRule(function(a, s) { if (a.id == "io.github.alldevic.plasma-nm-vpn.guard" && '
                    's.user == "pnv-test") return polkit.Result.YES; });\n')
    else:
        sh(f"rm -f {RULE}")
    time.sleep(1)


class Form:
    def __init__(self):
        self.proc = subprocess.Popen(["runuser", "-u", "pnv-test", "--", "env", "QT_QPA_PLATFORM=offscreen", FORM],
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                     text=True)
        self.state = self.read()

    def read(self):
        line = self.proc.stdout.readline()
        assert line, "the form exited"
        return json.loads(line)

    def send(self, command):
        self.proc.stdin.write(command + "\n")
        self.proc.stdin.flush()
        self.state = self.read()
        return self.state

    def close(self):
        self.proc.stdin.close()
        self.proc.wait(timeout=10)


def test_banner_shows_and_clears(names):
    form = Form()
    assert not form.state["visible"]
    form.close()
    names.append("pnv-g-one")
    uuid = block("hysteria2", "pnv-g-one")
    consent(False)
    form = Form()
    assert form.state["visible"] and form.state["entries"] == [uuid]
    assert "Kill switch block active" in form.state["text"] and "pnv-g-one" in form.state["text"]
    refused = form.send(f"clear {uuid}")
    assert refused["cleared"] is False and refused["visible"] and "was not cleared" in refused["text"]
    assert kill_switch(uuid) and entry(uuid)
    consent(True)
    cleared = form.send(f"clear {uuid}")
    assert cleared["cleared"] is True and not cleared["visible"]
    assert not kill_switch(uuid) and not entry(uuid)
    form.close()


@pytest.fixture
def held_attempt():
    """The next attempt of a VLESS profile stays open: its server port answers nothing and the test build's
    liveness check waits 120 s (5.4)."""
    def hold():
        with open(LIVENESS, "w") as f:
            f.write("120\n")
        sh(f"{N} add table inet tst-hold && {N} add chain inet tst-hold input "
           "'{ type filter hook input priority -10; }' && "
           f"{N} add rule inet tst-hold input tcp dport 443 drop")
    yield hold
    sh(f"{N} destroy table inet tst-hold", check=False)
    sh(f"rm -f {LIVENESS}")


def test_clear_all_keeps_the_live_profile(names, held_attempt):
    names += ["pnv-g-a", "pnv-g-b"]
    a, b = block("hysteria2", "pnv-g-a"), block("vless", "pnv-g-b")
    held_attempt()
    attempt = subprocess.Popen(["nmcli", "--wait", "150", "connection", "up", "pnv-g-b"], stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
    try:
        # NM shows the VPN activated once it has the Ip4Config; the first liveness check, which decides, comes after.
        assert wait_for(lambda: sh("nmcli -g GENERAL.STATE connection show pnv-g-b", check=False).strip()
                        == "activated", timeout=30)
        consent(True)
        form = Form()
        assert sorted(form.state["entries"]) == sorted([a, b])
        assert "clear-all" in form.state["actions"]
        assert "pnv-g-b" in form.state["text"] and "(connected)" in form.state["text"]
        after = form.send("clear-all")
        assert after["cleared"] is True and after["entries"] == [b] and "(connected)" in after["text"]
        assert not kill_switch(a) and not entry(a) and kill_switch(b)
        last = form.send(f"clear {b}")
        assert last["cleared"] is True and not last["visible"]
        assert not kill_switch(b) and not entry(b)
        form.close()
    finally:
        sh("nmcli connection down pnv-g-b", check=False)
        attempt.wait(timeout=30)
