# Integration cases run as root inside the test VM, with the bench up (guest/prepare).
# PNV_DNS_MODE is resolvconf or resolved; cases marked kvm need the VM's own kernel and run only with PNV_KVM=1,
# cases marked test_build need the daemons' test build installed (guest/install-daemon --test-hooks), PNV_TEST_BUILD=1.
import os
import subprocess

import pytest

DNS_MODE = os.environ.get("PNV_DNS_MODE", "resolvconf")


def pytest_configure(config):
    config.addinivalue_line("markers", "kvm: needs the test VM's own kernel (sleep, Wi-Fi emulation)")
    config.addinivalue_line("markers", "test_build: needs the never-packaged test build")


def pytest_collection_modifyitems(config, items):
    needs = {
        "kvm": (os.environ.get("PNV_KVM") == "1", "needs kvm: not a VM with its own kernel"),
        "test_build": (os.environ.get("PNV_TEST_BUILD") == "1", "needs the test build of the daemons"),
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
