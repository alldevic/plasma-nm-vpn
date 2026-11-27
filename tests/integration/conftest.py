# Integration cases run as root inside the test VM, with the bench up (guest/prepare).
# PNV_DNS_MODE is resolvconf or resolved; cases marked kvm need the VM's own kernel and run only with PNV_KVM=1.
import os
import subprocess

import pytest

DNS_MODE = os.environ.get("PNV_DNS_MODE", "resolvconf")


def pytest_configure(config):
    config.addinivalue_line("markers", "kvm: needs the test VM's own kernel (sleep, Wi-Fi emulation)")
    config.addinivalue_line("markers", "test_build: needs the never-packaged test build")


def pytest_collection_modifyitems(config, items):
    if os.environ.get("PNV_KVM") == "1":
        return
    skip = pytest.mark.skip(reason="needs kvm: not a VM with its own kernel")
    for item in items:
        if "kvm" in item.keywords:
            item.add_marker(skip)


def sh(cmd, check=True, timeout=60):
    """Run a shell command, return its stdout; fail the test with the output on error."""
    r = subprocess.run(cmd, shell=True, text=True, capture_output=True, timeout=timeout)
    if check and r.returncode:
        pytest.fail(f"{cmd}\nexit {r.returncode}\n{r.stdout}{r.stderr}")
    return r.stdout
