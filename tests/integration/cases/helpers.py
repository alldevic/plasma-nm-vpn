# Shared by the daemon cases: profiles, the journal, the units and resources of a profile.
import os
import subprocess
import time

from conftest import sh

SERVICE = "org.freedesktop.NetworkManager.pnv.hysteria2"
UNKNOWN = "org.freedesktop.NetworkManager.pnv.test-unknown"
PASSWORD = "s3cret-never-logged"
DAEMON = "/usr/lib/plasma-nm-vpn/plasma-nm-vpn-service-"


def ok(cmd):
    return subprocess.run(cmd, shell=True, capture_output=True, timeout=60).returncode == 0


def cursor():
    return sh("journalctl -n 0 --show-cursor --no-pager").splitlines()[-1].removeprefix("-- cursor: ")


def journal(after, *args):
    """The journal since the cursor; --sync first, so that every message written before the call is in it."""
    sh("journalctl --sync", timeout=30)
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


def keyfile(name, data, secrets=None, never_default=False, uuid=None, service=SERVICE):
    """A test .nmconnection (0600) in system-connections, loaded with nmcli; returns its UUID."""
    import uuid as uuidlib
    sh(f"nmcli connection delete {name}", check=False)
    uuid = uuid or str(uuidlib.uuid4())
    lines = ["[connection]", f"id={name}", f"uuid={uuid}", "type=vpn", "autoconnect=false", "",
             "[vpn]", f"service-type={service}", "persistent=true"]
    lines += [f"{k}={v}" for k, v in data.items()]
    lines += [f"{k}-flags=0" for k in (secrets or {})]
    if secrets:
        lines += ["", "[vpn-secrets]"] + [f"{k}={v}" for k, v in secrets.items()]
    lines += ["", "[ipv4]", "method=auto"] + (["never-default=true"] if never_default else []) + [""]
    path = f"/etc/NetworkManager/system-connections/{name}.nmconnection"
    with open(path, "w") as f:
        f.write("\n".join(lines))
    os.chmod(path, 0o600)
    sh(f"nmcli connection load {path}")
    return uuid


def no_traces(uuid, server_ips=("198.51.100.1", "2001:db8:100::1"), timeout=20):
    """Nothing of the session is left (2.7). Waits for the daemon to exit first."""
    def clean():
        problems = []
        if [p for p in scope_pids(uuid) if p in daemon_pid()] or daemon_pid():
            problems.append("daemon process")
        if sh("ip -br link show | grep -c '^pnv-' || true").strip() != "0":
            problems.append("pnv-* interface")
        routes = sh("ip route; ip -6 route")
        if " dev pnv-" in routes:
            problems.append("route via pnv-*")
        for ip in server_ips:
            if any(l.startswith(f"{ip} ") for l in routes.splitlines()):
                problems.append(f"host route to {ip}")
        table = sh("nft list table inet plasma_nm_vpn 2>/dev/null || true")
        if uuid in table or f"test-{uuid[:8]}" in table:
            problems.append("chains")
        if sh("systemctl list-units --all --no-legend --plain 'plasma-nm-vpn-*' || true").strip():
            problems.append("units")
        if sh("pgrep -u plasma-nm-vpn || true").strip():
            problems.append("plasma-nm-vpn processes")
        if run_dir_exists(uuid):
            problems.append("run directory")
        return problems
    wait_for(lambda: not clean(), timeout=timeout)
    return clean()


def ip4_dns(tun_name):
    """IP4.DNS of the tunnel device; NM may list the device a moment after the activation returns."""
    wait_for(lambda: ok(f"nmcli -g GENERAL.DEVICE device show {tun_name}"), timeout=10)
    return sh(f"nmcli -g IP4.DNS device show {tun_name}").strip()
