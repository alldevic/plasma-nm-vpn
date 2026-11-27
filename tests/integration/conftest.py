# Integration cases run as root inside the test VM, with the bench up (guest/prepare).
# PNV_DNS_MODE is resolvconf or resolved; cases marked kvm need the VM's own kernel and run only with PNV_KVM=1,
# cases marked test_build need the daemons' test build, PNV_TEST_BUILD=1: in CI the harness passes its DESTDIR tree
# as PNV_TEST_BUILD_DIR and each such case runs with it bind-mounted over the packaged binaries; locally,
# guest/install-daemon --test-hooks installs it instead. Cases marked packages need the .deb files installed
# (PNV_PACKAGES=1).
import os
import subprocess

import pytest

DNS_MODE = os.environ.get("PNV_DNS_MODE", "resolvconf")


def pytest_configure(config):
    config.addinivalue_line("markers", "kvm: needs the test VM's own kernel (sleep, Wi-Fi emulation)")
    config.addinivalue_line("markers", "test_build: needs the never-packaged test build")
    config.addinivalue_line("markers", "packages: needs the system under test installed from the .deb files")


def pytest_collection_modifyitems(config, items):
    needs = {
        "kvm": (os.environ.get("PNV_KVM") == "1", "needs kvm: not a VM with its own kernel"),
        "test_build": (os.environ.get("PNV_TEST_BUILD") == "1", "needs the test build of the daemons"),
        "packages": (os.environ.get("PNV_PACKAGES") == "1", "needs the packages installed"),
    }
    for item in items:
        for marker, (available, reason) in needs.items():
            if marker in item.keywords and not available:
                item.add_marker(pytest.mark.skip(reason=reason))


def sh(cmd, check=True, timeout=60):
    """Run a shell command, return its stdout; fail the test with the output on error."""
    r = subprocess.run(cmd, shell=True, text=True, capture_output=True, timeout=timeout)
    if check and r.returncode:
        pytest.fail(f"{cmd}\nexit {r.returncode}\n{r.stdout}{r.stderr}")
    return r.stdout


TEST_BUILD_DIR = os.environ.get("PNV_TEST_BUILD_DIR")
TEST_BINARIES = ["usr/lib/plasma-nm-vpn/plasma-nm-vpn-service-sb", "usr/bin/plasma-nm-vpn-guard"]


@pytest.fixture(autouse=True)
def test_build(request):
    """The test build over the packaged binaries, for this case only; never installed through a package."""
    if "test_build" not in request.keywords or not TEST_BUILD_DIR:
        yield
        return
    for b in TEST_BINARIES:
        sh(f"mount --bind {TEST_BUILD_DIR}/{b} /{b}")
    try:
        yield
    finally:
        for b in TEST_BINARIES:
            sh(f"umount /{b}", check=False)
