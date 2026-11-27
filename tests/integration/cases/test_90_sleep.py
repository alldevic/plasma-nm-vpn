# A real suspend (S3, 0.2) woken by the RTC; runs last, since the VM sleeps. The harness reaches the VM
# over vsock throughout, so this case returning at all shows the session survived.
import time

import pytest

from conftest import sh


@pytest.mark.kvm
def test_suspend_and_rtc_wake():
    since = sh("date '+%F %T'").strip()
    sh("rtcwake -m no -s 20")
    sh("systemctl suspend")
    for _ in range(60):
        time.sleep(2)
        if "PM: suspend exit" in sh(f"journalctl -k --since '{since}' --no-pager", check=False):
            break
    else:
        pytest.fail("no resume within 120 s")
    log = sh(f"journalctl -k --since '{since}' --no-pager")
    assert "PM: suspend entry (deep)" in log
