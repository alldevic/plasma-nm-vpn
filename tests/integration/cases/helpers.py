# Shared by the daemon cases: profiles, the journal, the units and resources of a profile.
import subprocess
import time

from conftest import sh

SERVICE = "org.freedesktop.NetworkManager.pnv.hysteria2"
UNKNOWN = "org.freedesktop.NetworkManager.pnv.test-unknown"
PASSWORD = "s3cret-never-logged"
DAEMON = "/usr/lib/plasma-nm-vpn/plasma-nm-vpn-service-sb"


def ok(cmd):
    return subprocess.run(cmd, shell=True, capture_output=True, timeout=60).returncode == 0


def cursor():
    return sh("journalctl -n 0 --show-cursor --no-pager").splitlines()[-1].removeprefix("-- cursor: ")


def journal(after, *args):
    return sh(f"journalctl --after-cursor '{after}' --no-pager -o cat {' '.join(args)}", timeout=30)


def wait_for(predicate, timeout=20):
    for _ in range(timeout * 4):
        if predicate():
            return True
        time.sleep(0.25)
    return False


def profile(name, service=SERVICE, data="server=198.51.100.1, ports=443", flags=0, uuid=None, password=None):
    sh(f"nmcli connection delete {name}", check=False)
    extra = f" connection.uuid {uuid}" if uuid else ""
    extra += f" vpn.secrets 'password={password}'" if password else ""
    sh(f"nmcli connection add type vpn con-name {name} autoconnect no vpn-type {service} "
       f"vpn.data '{data}, password-flags={flags}'{extra}")
    return name


def uuid_of(name):
    return sh(f"nmcli -g connection.uuid connection show {name}").strip()


def up(name, *args):
    """nmcli connection up in the background."""
    return subprocess.Popen(["nmcli", "connection", "up", name, *args], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)


def daemon_pid():
    return sh(f"pgrep -f '^{DAEMON}' || true").split()


def units(uuid):
    """The units of a profile systemd still knows, with their ActiveState."""
    out = sh(f"systemctl list-units --all --no-legend --plain 'plasma-nm-vpn-*-{uuid}.*'", check=False)
    return {line.split()[0]: line.split()[2] for line in out.splitlines() if line.strip()}


def scope_pids(uuid):
    cg = sh(f"systemctl show -p ControlGroup --value plasma-nm-vpn-daemon-{uuid}.scope", check=False).strip()
    if not cg:
        return []
    return sh(f"cat /sys/fs/cgroup{cg}/cgroup.procs", check=False).split()


def tun(uuid):
    return f"pnv-{uuid[:8]}"


def tun_exists(uuid):
    return ok(f"ip link show {tun(uuid)}")


def run_dir_exists(uuid):
    return ok(f"test -d /run/plasma-nm-vpn/{uuid}")


def core_pid(uuid):
    """The core's PID, from its unit (the bench runs a sing-box of its own)."""
    pid = sh(f"systemctl show -p MainPID --value plasma-nm-vpn-core-{uuid}.service", check=False).strip()
    return pid if pid not in ("", "0") else None


def gone(uuid):
    """Nothing of the profile's session is left: no unit (so no core process), TUN or run directory."""
    return not units(uuid) and not tun_exists(uuid) and not run_dir_exists(uuid)
