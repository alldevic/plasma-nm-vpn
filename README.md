# plasma-nm-vpn

[Русская версия](README.ru.md)

NetworkManager VPN plugins and KDE Plasma editors for three censorship-resistant protocols, with a kill switch:

| Protocol | Package | Core |
|----------|---------|------|
| Hysteria2 (Salamander obfuscation, port hopping) | `plasma-nm-vpn-sb` | sing-box |
| VLESS XHTTP Reality (with VLESS Encryption) | `plasma-nm-vpn-xray` | Xray |
| AmneziaWG 3.1 | `plasma-nm-vpn-amneziawg` | amneziawg-go and awg |

`plasma-nm-vpn` installs all three; `plasma-nm-vpn-common` holds what they share. Profiles are created and connected
from Plasma's network settings and applet, like any other VPN.

**Platform:** Debian forky (testing) with KDE Plasma 6, amd64. Debian sid works too. Other distributions and
architectures are not supported.

## Installation

Download `plasma-nm-vpn-archive-keyring_*_all.deb` from the
[latest release](https://github.com/alldevic/plasma-nm-vpn/releases/latest). It adds the project's signing key and
apt source. Then:

```
sudo apt install ./plasma-nm-vpn-archive-keyring_*_all.deb
sudo apt update
sudo apt install plasma-nm-vpn
```

To install only some protocols, install their packages instead of `plasma-nm-vpn`, for example
`sudo apt install plasma-nm-vpn-sb`.

Log out and back in (or reboot) after installing. Plasma's settings and its secret agent load the editors when they
start.

The apt repository lives in the release assets. For about two minutes after a new release, `apt update` may fail once
with “Hash Sum mismatch” (GitHub caches its redirects); run it again. If `apt update` keeps reporting a signature
error for this repository, remove the stale partial file and retry:

```
sudo rm /var/lib/apt/lists/partial/*plasma-nm-vpn*
```

### Updating

`sudo apt update && sudo apt upgrade` updates the packages, cores included. **Log out and back in after an update.**
System Settings and Plasma's secret agent (kded6) keep the editors they loaded. Until they restart, a VPN entry may be
missing from the settings and the password prompt may not appear. Connections that are up keep running through an
update.

## Creating a profile

System Settings → Wi-Fi & Networking (or the network applet → Configure) → **+** → pick **Hysteria2**,
**VLESS XHTTP Reality** or **AmneziaWG** under “VPN connections” → fill in the **VPN (hysteria2)**, **VPN (vless)**
or **VPN (amneziawg)** tab → **Save**. Connect from the network applet.

The form checks every field as you type and explains what is wrong under the field's group; **Save** stays disabled
until the form is valid.

**Secrets** (password, UUID, keys) have the usual Plasma storage choices:

- “Store password for this user only (encrypted)”, the default when a keychain (KWallet) is available. Enter the secret
  in the editor and save. A secret typed only at the connect prompt is not kept: Plasma asks again next time.
- “Store password for all users (not encrypted)”: kept in the system's connection file, readable by root only.
- “Ask for this password every time”: the applet asks on each connect.
- “This password is not required”, for optional secrets (the Hysteria2 obfuscation password, the AmneziaWG preshared
  and header protection keys).

**DNS:** set the DNS servers of your VPN provider in **Protection**. If the field is empty and all traffic goes
through the VPN, 1.1.1.1 (Cloudflare) and 9.9.9.9 (Quad9) are used.

### Import and export

Every editor has an **Import and Export** group at the top:

- Paste a `hysteria2://` or `hy2://` link, a `vless://` link or an AmneziaWG `.conf` into the field and press
  **Import**, or press **Open File…**. The form is filled in. Anything the form has no field for is listed: dropped,
  or carried into **Additional parameters** where the core supports it. Check the fields, set the DNS if you want,
  and save. A link for another protocol tells you which VPN type to add instead.
- **Export…** gives the profile back as a link or a `.conf`, to copy or to save to a file. Secrets are left out unless
  you check **Include secrets**. A file with secrets is readable by you only. A link copied with its secrets may stay
  in the clipboard history. Fields the format cannot carry (the kill switch, for example) are listed.

Not supported: Hysteria2 links with `obfs=gecko`, `pinSHA256` or `ech`, `hysteria2+realm://`; `vless://` links with
another transport than XHTTP or another security than Reality; AmneziaWG files with more than one `[Peer]`. Import
says so.

Use the field in our editors, not Plasma's **Import VPN connection…**. Plasma's import works with files only, and it
gives a `.conf` to WireGuard or OpenVPN. An AmneziaWG `.conf` fails there (“NetworkManager is missing support for
OpenVPN”) and never becomes an AmneziaWG profile. Plasma's **Export selected connection** is disabled for our
profiles; use **Export…** in the editor.

## Kill switch

**Block traffic outside the VPN (kill switch)** in **Protection** keeps traffic from leaving past the tunnel when it
drops. While the VPN reconnects, after a network change or after sleep, nothing goes out directly. If the connection
cannot be restored, the block stays until you clear it.

- It applies only when all traffic goes through the VPN. With “Use only for resources on this connection” in the
  IPv4 tab, it is ignored (the journal says so). In AmneziaWG it is unavailable unless **Allowed IPs** contain
  `0.0.0.0/0`.
- **Allow the local network while blocked** (on by default) keeps your LAN (192.168.x.x, 10.x.x.x and so on)
  reachable during a block. DNS to LAN addresses is blocked anyway, so a blocked machine does not ask the router for
  names.
- Disconnecting yourself, or deleting the profile, always removes the block.

### No internet after a VPN failure

If the VPN gave up with the kill switch on, or its service crashed, the block remains on purpose. Open the profile in
the settings: the **Kill switch block active** banner lists every blocked profile.

- **Clear** removes that profile's block after asking for the administrator password.
- **Clear All** removes every block except those of connections that are up or connecting.
- Connecting the profile again also works: once the new connection succeeds, it takes the block over.

From a terminal: `plasma-nm-vpn-guard --clear <uuid>` clears one profile (the UUID is in
`nmcli -g connection.uuid connection show <name>`); `plasma-nm-vpn-guard --clear` clears all blocks except those of
connections that are up or connecting. Both ask for the administrator password. Rebooting also clears all blocks.

If the server changed its IP address: under a block, the VPN can only reach the address it last used. Clear the block
first, then connect, so that the new address is looked up.

## Diagnostics

- The daemon: `journalctl -t plasma-nm-vpn-service-sb` (or `-service-xray`, `-service-awg`);
- the core of a connection: `journalctl -u plasma-nm-vpn-core-<uuid>.service`;
- NetworkManager: `journalctl -u NetworkManager`.

Secrets never appear in these logs.

## Network access

The plugins connect to the VPN server and nothing else. Over the tunnel they check that it works, once a minute, with
a DNS query to the profile's first DNS server (to 1.1.1.1 if the profile sets no DNS and does not route all traffic).
No telemetry, no update checks.

## Removing

`sudo apt remove plasma-nm-vpn-sb` (or another core package) first disconnects that protocol's connections.
Removing `plasma-nm-vpn-common` also removes the kill switch firewall rules and any block left. The system user
`plasma-nm-vpn`, which the cores run as, stays after `purge`. Profiles stay in NetworkManager until you delete them.

## Licenses

The project is MIT (see [`LICENSE`](LICENSE)). The built editors link against plasma-nm's internal library, which is
GPL, so a built editor together with it forms a combined work under the GPL. The sources here remain MIT.

The packages ship these cores unchanged:

| Core | License | Sources |
|------|---------|---------|
| [sing-box](https://github.com/SagerNet/sing-box) 1.14.6 | GPL-3.0-or-later, with the additional term: “In addition, no derivative work may use the name or imply association with this application without prior consent” | `sing-box-*-source.tar.xz` (with the Go dependencies) in each release's assets |
| [Xray](https://github.com/XTLS/Xray-core) 26.11.9 | MPL-2.0 | [XTLS/Xray-core](https://github.com/XTLS/Xray-core/tree/v26.11.9) |
| [amneziawg-go](https://github.com/amnezia-vpn/amneziawg-go) 3.1.20261116 | MIT | [amnezia-vpn/amneziawg-go](https://github.com/amnezia-vpn/amneziawg-go) |
| awg from [amneziawg-tools](https://github.com/amnezia-vpn/amneziawg-tools) 3.1.20260812 | GPL-2.0 | `amneziawg-tools-*-source.tar.xz` in each release's assets |

This project is not associated with sing-box. That is why its package is named `plasma-nm-vpn-sb` and none of its
identifiers contains the name. Each package carries its core's license in `/usr/share/doc/<package>/`. The versions
above are those pinned in [`cores/`](cores/); the release notes list them for each release.

The plasma-nm headers in [`third_party/plasma-nm/`](third_party/plasma-nm/) are `LGPL-2.1-only OR LGPL-3.0-only OR
LicenseRef-KDE-Accepted-LGPL`, with their texts there.

## Security

See [`SECURITY.md`](SECURITY.md). The daemons run as root, so please report vulnerabilities privately.

## Development

The design and the plan are in [`docs/`](docs/).

Build dependencies on forky: `build-essential cmake pkgconf libnm-dev libglib2.0-dev libjson-glib-dev` and, for the
editors, `qt6-base-dev extra-cmake-modules libkf6coreaddons-dev libkf6widgetsaddons-dev libkf6i18n-dev
libkf6networkmanagerqt-dev plasma-nm`.

```
cmake -B build && cmake --build build && ctest --test-dir build
```

Installing into `/usr` needs `-DCMAKE_INSTALL_PREFIX=/usr -DKDE_INSTALL_USE_QT_SYS_PATHS=ON`, so that the editors land
in Qt's plugin directory.

The cores are pinned in [`cores/`](cores/); `scripts/fetch-cores` downloads or builds them and checks every hash
(it needs network access, Go, git, curl and unzip). Then the package build itself runs offline:

```
scripts/fetch-cores cores-staging
dpkg-buildpackage -b -us -uc
```

Translations: `scripts/extract-messages` updates `po/plasma-nm-vpn.pot` and merges it into `po/<lang>/plasma-nm-vpn.po`.
