# Work plan: stages and milestones

Revised 26 November 2026. Based on [design.md](design.md). Each milestone is described in detail in a separate file in the [milestones/](milestones/) directory.

- **Durations** are working days of a single developer; no milestone is longer than 2 days.
- **Every milestone** ends with a verifiable result (the “Done when” column). If a milestone does not fit its duration, it is split rather than stretched.
- **Network experiments** are done only in the test VM, in CI or on a separate hardware test machine, not on the work machine.
- **Order:** stages go one after another, and so do the milestones within a stage, unless stated otherwise. The backend order (Hysteria2 → AmneziaWG → xray) and the rule “UI only after the tunnels come up reliably via nmcli” come from the design.

## Summary

| Stage | Content | Milestones | Days |
|------|------------|-----|------|
| 0 | Test bench and prototypes: remove the remaining technical risks | 9 | 11.5 |
| 1 | Infrastructure: repository, packages, release pipeline, CI | 5 | 9 |
| 2 | Shared library and Hysteria2 via nmcli | 7 | 14 |
| 3 | AmneziaWG via nmcli | 2 | 4 |
| 4 | VLESS XHTTP Reality via nmcli | 2 | 4 |
| 5 | Kill switch, inherited block, block status, liveness check, reconnection, network changes and sleep | 8 | 15 |
| 6 | plasma-nm editors, kill switch banner, import and export | 9 | 16 |
| 7 | MVP release | 3 | 5 |
| **MVP** | | **45** | **≈ 78.5 days (≈ 15.5 weeks)** |
| 8 | Phase 2 | — | estimated after the MVP |

## Stage 0. Test bench and prototypes

The goal is to verify everything the design rests on before writing any product code. Prototype code is thrown away; only the conclusions carry over into the product, and `design.md` is updated based on the results of the stage. Two exceptions: the server bench scripts from 0.3 become the product test bench, and the plugin prototype from 0.7 serves as a temporary payload for the CI test (1.4) and is removed in 2.7.

| Milestone | Days | What gets done | Done when |
|------|------|-------------|---------------|
| [0.1 sing-box question](milestones/0.1-singbox-name-issue.md) | 0.5 | An issue in sing-box: does the name term in the license allow the package name `plasma-nm-vpn-sing-box` | The issue is created. We wait for the answer in parallel; it is needed by milestone 7.1 |
| [0.2 Test VM](milestones/0.2-test-vm.md) | 1 | A VM with Debian forky and KDE Plasma 6 (libvirt/QEMU), a snapshot of the clean state | The VM reverts to the snapshot with a single command |
| [0.3 Server bench](milestones/0.3-server-bench.md) | 2 | Scripts that bring up servers in a network namespace: hysteria2 on sing-box, VLESS XHTTP Reality on xray, AmneziaWG on amneziawg-go; veth to the “client” side | Each server is brought up and stopped by a script; a manually run client reaches it |
| [0.4 Cores without root](milestones/0.4-cores-without-root.md) | 2 | sing-box, xray and amneziawg-go started via `systemd-run` as a user with `CAP_NET_ADMIN CAP_NET_RAW` in a sandbox; for amneziawg-go, `/run/amneziawg` via tmpfiles.d and `awg setconf` as root | All three cores attach to a daemon-created persistent TUN and pass traffic to the bench; a core restart keeps the interface and its routes |
| [0.5 Core lifetime](milestones/0.5-core-lifetime.md) | 1 | The process moves itself into a transient scope; a core unit and a guard unit (`Type=oneshot`, `RemainAfterExit=yes`, root `ExecStop`, core `After=` guard) with `BindsTo=` on the scope, created over D-Bus with `LoadCredential=` | `kill -9` of the “daemon” stops the core first, then the guard unit's `ExecStop` runs as root; the config is read from credentials |
| [0.6 xray TUN](milestones/0.6-xray-tun.md) | 1 | xray ≥ v26.10.21: TUN inbound, `sockopt.interface`, default route through the TUN, attaching to a daemon-created persistent TUN | No loop; TCP and UDP (DNS) pass through VLESS XHTTP |
| [0.7 Minimal NM plugin](milestones/0.7-minimal-nm-plugin.md) | 2 | A daemon on `NMVpnServicePlugin` with `.name` and a D-Bus policy; starts sing-box in a unit with the scope from 0.5 and hands over Config (`TUNDEV`, `gateway`, `CAN_PERSIST`) and `Ip4Config`; the `STARTING` cycle → handing over the configuration again | `nmcli connection up` brings up the tunnel; after `STARTING` the connection returns to ACTIVATED with updated routes and DNS; moving the daemon out of the NetworkManager cgroup does not interfere with NM; the deactivation reasons for `down`, re-activation and an NM restart are recorded |
| [0.8 Minimal editor](milestones/0.8-minimal-editor.md) | 1 | An empty `VpnUiPlugin` outside the plasma-nm tree, built against headers from tag 6.7.4 | The entry is visible in the “Add VPN” list, the form opens, the profile is saved |
| [0.9 Flat repository](milestones/0.9-flat-apt-repo.md) | 1 | A test GitHub repository: draft release, assets with `+` in the name, a signed `InRelease` | `apt update` and `apt install` succeed in the VM; the asset naming rules are confirmed |

**Stage outcome:** all “Verify on a prototype (stage 0)” items in `design.md` are closed or reworked in the design, except the `/dev/vhost-vsock` item, which milestone 1.4 closes.

## Stage 1. Infrastructure

| Milestone | Days | What gets done | Done when |
|------|------|-------------|---------------|
| [1.1 Repository skeleton](milestones/1.1-repo-skeleton.md) | 2 | Directory layout, CMake for the daemons (C) and editors (C++), LICENSE (MIT), README and `SECURITY.md` stubs, vendored plasma-nm headers; CI builds everything in a forky container; actions pinned by SHA, minimal token permissions | Empty daemon and editor build in CI on every PR |
| [1.2 Packages](milestones/1.2-packages.md) | 2 | `debian/` for `plasma-nm-vpn-common`, the three core packages, the metapackage and `plasma-nm-vpn-archive-keyring`; repackaging of sing-box and xray with sha256 verification against `.dgst` and the `digest` from the API; building amneziawg-go and `awg` from tags | CI produces all .deb files; they install in the VM and are removed without leftovers (except the system user `plasma-nm-vpn`, as is customary in Debian) |
| [1.3 Release pipeline](milestones/1.3-release-pipeline.md) | 2 | Signing in a GitHub Environment with manual approval, a flat repository in the release assets, attestations, publication of the GPL core sources | A test release in the test repository from 0.9 (with the test key) installs in the VM via `plasma-nm-vpn-archive-keyring` and `apt install plasma-nm-vpn` |
| [1.4 Integration test bench in CI](milestones/1.4-ci-integration-bench.md) | 2 | A forky VM under KVM on a GitHub runner (or a privileged container with systemd) and the server bench from 0.3 | The test “the 0.7 prototype brings up a tunnel via nmcli” passes in CI |
| [1.5 Upstream watch](milestones/1.5-upstream-watch.md) | 1 | A scheduled CI job: compares the plugin headers with plasma-nm in sid and tracks new versions of sing-box, xray, amneziawg-go and amneziawg-tools | An issue is created automatically when the headers diverge or a new core version appears |

## Stage 2. Shared library and Hysteria2

The largest stage: this is where everything the other cores later reuse gets built.

| Milestone | Days | What gets done | Done when |
|------|------|-------------|---------------|
| [2.1 Daemon skeleton](milestones/2.1-daemon-skeleton.md) | 2 | The `plasma-nm-vpn-common` library: a class on `NMVpnServicePlugin`, `--bus-name`, protocol detection from `vpn.service-type`, a shared `need_secrets` mechanism, states, `Failure` with an `NM_VPN_PLUGIN_FAILURE_*` code (the reason goes to the journal), logging; `.name` (`org.freedesktop.NetworkManager.pnv.hysteria2`) and the D-Bus policy | The daemon registers, accepts `Connect`/`Disconnect`, requests missing secrets and reports errors correctly |
| [2.2 Schema and field validation](milestones/2.2-profile-schema-validation.md) | 2 | Profile schema (`schema=1`), validators (hosts, ports, base64, lengths, control characters), allowlists as data; unit tests and a fuzzing harness | Known vectors in the tests (newlines, paths in `extra`) are rejected; fuzzing runs on a schedule |
| [2.3 Core launch](milestones/2.3-core-launch.md) | 2 | On `Connect`: orphan cleanup (inactive units only; waiting for a still-active previous instance; TUN name collision check), the daemon scope, the guard unit, the transient core unit (user, capabilities, sandbox, `LoadCredential=`, `BindsTo=`, `CollectMode=inactive-or-failed`), waiting for the TUN via netlink with a timeout, synchronous stop | The core starts and stops; `kill -9` of the daemon leaves no processes and no TUN; re-activating a profile does not kill its live previous instance |
| [2.4 Network parameters](milestones/2.4-network-parameters.md) | 2 | Resolving the server (IPv4 preferred, 5 s timeout) and the cache in `/var/lib/plasma-nm-vpn/<uuid>` (server value + IP, written only after success, `m:90d`), the persistent TUN, a /30 subnet from 198.18.0.0/15 under a lock, Config (`TUNDEV`, `gateway`, `CAN_PERSIST`), `Ip4Config` (address; DNS defaults 1.1.1.1 and 9.9.9.9 only for default-route tunnels, /32 routes for explicit DNS with never-default), binding to the VPN's parent device | NM sets the address on the TUN, the route to the gateway and DNS |
| [2.5 sing-box config](milestones/2.5-singbox-config.md) | 2 | A Hysteria2 config generator (port hopping, Salamander, TLS) for the pinned sing-box version 1.14.x, the secrets `password` and `obfs-password`, allowlisted `extra`, golden tests | Golden tests pass; `sing-box check` accepts the generated config |
| [2.6 DNS and IPv6 guard](milestones/2.6-dns-ipv6-guard.md) | 2 | Table `inet plasma_nm_vpn` with per-UUID chains on the `output` and `forward` hooks: reject IPv6 and DNS that bypass the tunnel when it carries the default route; loopback and core traffic are not affected; chains installed only after the guard unit runs; ICMPv6 ND allowed to any address; removal on `Disconnect`, and by `plasma-nm-vpn-guard --daemon-gone` from the guard unit if the daemon crashed | In the VM, a capture on the physical interface shows no DNS and no IPv6 while the tunnel is up |
| [2.7 Hysteria2 integration tests](milestones/2.7-hysteria2-tests.md) | 2 | CI tests: connection, traffic, DNS and IPv6 leaks, cleanup after `Disconnect` and after `kill -9` of the daemon, `kill -9` of the core before success (give-up), known attack vectors via the profile (newlines, paths in `extra`) through `nmcli`; removing the 0.7 prototype from CI | The suite is green in CI |

**Stage outcome:** Hysteria2 works via `.nmconnection` and `nmcli`.

## Stage 3. AmneziaWG

| Milestone | Days | What gets done | Done when |
|------|------|-------------|---------------|
| [3.1 AmneziaWG daemon](milestones/3.1-awg-daemon.md) | 2 | Generating the config for `awg setconf` from profile fields, an allowlist of AWG 3.1 parameters (data), the secrets `private-key`, `preshared-key`, `awg-headerprotectionkey`, running amneziawg-go in a unit, `ReadWritePaths=/run/amneziawg`, a minimal `Ip4Config` (the first IPv4 from `address`), a handshake check after activation | The tunnel to the bench comes up, the handshake succeeds |
| [3.2 Routes and tests](milestones/3.2-awg-routes-tests.md) | 2 | `AllowedIPs` → routes in `Ip4Config` per the AmneziaWG rules in `design.md`, DNS (with defaults) and MTU; integration tests modeled on 2.7 | The suite is green in CI |

## Stage 4. VLESS XHTTP Reality

| Milestone | Days | What gets done | Done when |
|------|------|-------------|---------------|
| [4.1 xray config](milestones/4.1-xray-config.md) | 2 | Generator: VLESS (with `encryption`), XHTTP, Reality, TUN inbound, `sockopt.interface`; allowlists for `extra` and `xhttp-extra`; golden tests | Golden tests pass; `xray run -test` accepts the config |
| [4.2 xray daemon and tests](milestones/4.2-xray-daemon-tests.md) | 2 | A daemon on the shared library, the `uuid` secret, integration tests, including UDP (DNS) through the tunnel | The suite is green in CI |

## Stage 5. Resilience

| Milestone | Days | What gets done | Done when |
|------|------|-------------|---------------|
| [5.1 Kill switch chains](milestones/5.1-kill-switch.md) | 2 | Per-UUID kill switch chains (only for default-route tunnels; exceptions: loopback, all our `pnv-*` tunnels, DHCP, DHCPv6, ND to any address, MLD, core traffic, local networks with `allow-lan=yes`; DNS rejected to every non-tunnel destination incl. the LAN); success marker; explicit disconnect via `USER_DISCONNECTED`/`CONNECTION_REMOVED`, give-up flag, fail closed after success, a session that never succeeded removes its chains; the kill switch part of `--daemon-gone` | After success, `kill -9` of the daemon lets no traffic out directly; a session that never reached success (a core that fails to start, a daemon held before ACTIVATED by a test-build hook) leaves no chains; a kill switch profile and a second connection pass traffic through each other's tunnels; an NM restart keeps the block; DNS to the LAN router is rejected while blocked |
| [5.2 Inherited block and takeover](milestones/5.2-inherited-takeover.md) | 2 | The “inherited block” marker: an attempt on leftover chains installs none of its own, works under them, swaps them atomically on success (or removes them if the profile no longer qualifies), keeps them on failure and on explicit disconnect; the cache rules for such attempts; re-activation (takeover) detected via libnm or the takeover marker, as found in 0.7 | Leftover chains survive a failed attempt and an explicit disconnect; a successful attempt replaces them; a normal re-activation keeps the kill switch without a window and without a banner |
| [5.3 Block status and clearing](milestones/5.3-block-status.md) | 1 | `/run/plasma-nm-vpn/guard-status` (format fixed here), entries written by the daemon and by `--daemon-gone`; `plasma-nm-vpn-guard --clear [<uuid>]` that re-executes itself via `pkexec`, and its polkit action | `guard-status` reflects blocks, including after a daemon crash; `--clear` from a regular user asks for administrator credentials and removes the chains; a reboot leaves nothing |
| [5.4 Liveness check](milestones/5.4-liveness-check.md) | 2 | A DNS query through the TUN to the profile's first DNS server for sing-box and xray, the handshake (and its age) for AmneziaWG; the success marker only after the check; a periodic check every 60 s, three failures in a row → reconnect cycle (until 5.5: give-up) | A wrong password or a dead server → give-up, no kill switch chains left; a server stopped while connected is detected within about three minutes (five to six for AmneziaWG) |
| [5.5 Reconnection](milestones/5.5-reconnect.md) | 2 | Tracking the parent device via libnm and sleep via logind, the cycle `STARTING` → recreating the core unit on the persistent TUN with the per-core post-start step (`awg setconf` for AmneziaWG) → handing over Config and `Ip4Config` again → liveness check, retries from step 1 or 2; waiting for a device within the cycle timeout (value fixed here); give-up via `nm_vpn_service_plugin_failure`; a crashed core after success goes through the same cycle | In the VM, the tunnel recovers after a parent device change and after an s2idle suspend; with `kill-switch=no` no IPv4 leaves directly during the cycle; with the server stopped, the cycle gives up after the timeout |
| [5.6 Kill switch tests](milestones/5.6-kill-switch-tests.md) | 2 | Updating the 2.7/3.2/4.2 core-kill suites (after success → recovery); integration tests: kill switch after success on a daemon crash, a failed first connection leaves no block, NM restart keeps the block, profile deletion, DNS to the LAN rejected while blocked, ND with the kill switch and an IPv6-only server | The suite is green in CI |
| [5.7 Block and connection tests](milestones/5.7-block-connection-tests.md) | 2 | Integration tests: an inherited block survives a failed attempt and an explicit disconnect, re-activation (takeover), `--clear` with and without UUID, liveness detection (wrong password, stopped server), two simultaneous connections including a never-default one whose liveness check to 1.1.1.1 does not take over the other's DNS | The suite is green in CI |
| [5.8 Network change and sleep tests](milestones/5.8-network-sleep-tests.md) | 2 | Integration tests on a bench with two parent devices (emulated Wi-Fi and veth): parent device change, sleep (s2idle suspend woken by `rtcwake`), core crash after success → recovery; measuring the recovery time over several runs | The suite is green in CI; the acceptable recovery time is recorded in the MVP acceptance criteria |

## Stage 6. plasma-nm editors

The parsers from milestone 6.6 do not depend on the editors and can be written earlier, once 4.1 is done (they use the allowlists of 2.5, 3.1 and 4.1), in parallel with stage 5. The import UI (6.7) and the later milestones require the finished editors from 6.3–6.5.

| Milestone | Days | What gets done | Done when |
|------|------|-------------|---------------|
| [6.1 Shared widgets](milestones/6.1-shared-widgets.md) | 2 | Shared form blocks in `plasma-nm-vpn-common` (server, TLS, authentication, protection, advanced) using the 2.2 validator library (allowlists included), rendering the validator message codes via ki18n (6.7 adds the parser codes, 6.8 the export codes), strings via ki18n, metadata with `X-NetworkManager-Services` | The widgets build and are covered by simple tests |
| [6.2 Kill switch banner](milestones/6.2-kill-switch-banner.md) | 1 | A shared widget in `plasma-nm-vpn-common`: reads `guard-status`, lists all active blocks with profile names (via NetworkManagerQt), “Clear” runs `plasma-nm-vpn-guard` (pkexec), refreshes | The “Kill switch block active” banner appears while a block exists and disappears after “Clear” |
| [6.3 Hysteria2 editor](milestones/6.3-hysteria2-editor.md) | 2 | The form, secrets via `PasswordField` with flags (“Not required” for `obfs-password`), `vpn.persistent=yes` on every save, the banner from 6.2 | A profile created in plasma-nm connects from the applet |
| [6.4 VLESS XHTTP Reality editor](milestones/6.4-vless-editor.md) | 2 | The same for VLESS, with its own Reality and XHTTP blocks and the value lists (fingerprints, XHTTP modes) from the 4.1 data | The same |
| [6.5 AmneziaWG editor](milestones/6.5-awg-editor.md) | 2 | Interface and Peer, an allowlist-based parameter table, `HeaderProtectionKey` as a secret (“Not required” for optional secrets) | The same |
| [6.6 Import parsers](milestones/6.6-import-parsers.md) | 2 | Parsers for `hysteria2://`/`hy2://`, `vless://`, `.conf` with all the AmneziaWG rules (dropping wg-quick keys, `dns` left empty when the `.conf` has none); message codes added to the shared C list (rendered later in 6.7); unit tests including attack vectors; fuzz targets in CI | All parser tests pass; fuzzing runs on a schedule |
| [6.7 Import UI](milestones/6.7-import-ui.md) | 2 | A “Paste link or config” field and an “Open file” button, the list of unsupported or dropped parameters, wiring into the three editors with confirmation before replacing filled-in fields | Links and `.conf` files from the bench fill in the forms of all three editors |
| [6.8 Export](milestones/6.8-export.md) | 2 | An “Export” button in the editors, generators for the same formats, export message codes with their ki18n entries, secrets only by explicit choice (file with mode 0600 or the clipboard) | Export → import preserves every field the format carries |
| [6.9 UI check](milestones/6.9-ui-check.md) | 1 | A check that plasma-nm's stock “Import VPN connection” and “Export” load our plugin without side effects and that it declines; the manual UI checklist, including the banner | The checklist is passed |

## Stage 7. MVP release

| Milestone | Days | What gets done | Done when |
|------|------|-------------|---------------|
| [7.1 Documentation and translation](milestones/7.1-docs-translation.md) | 2 | The sing-box package name is settled first (permission from the 0.1 issue, otherwise a neutral name); README in English and Russian with installation instructions (including restarting the session after an upgrade and the “No internet after a VPN failure” section), core license information and the vendored LGPL headers, UI translation and `Name[ru]`/`Description[ru]` in the metadata | A person with no context installs the packages in a clean VM following the README (from the test repository) |
| [7.2 Acceptance](milestones/7.2-acceptance.md) | 2 | Running all MVP acceptance criteria from `design.md` on a clean VM installed from the test repository; Wi-Fi ↔ Ethernet switching on the hardware test machine (over ssh), if it is available; fixes | All criteria are met, except installation from the public repository, which is checked in 7.3 |
| [7.3 Publication](milestones/7.3-publication.md) | 1 | Release 0.1.0 under the package names settled in 7.1 | The release is published, installation from the public repository works |

## Stage 8. Phase 2 (after the MVP)

The detailed breakdown into milestones comes after the MVP: some items depend on open questions in `design.md`. Tentatively:

| Milestone candidate | Estimate | Depends on |
|-----------------|----------------|------------|
| TUIC (daemon, editor, import, tests) | 2 milestones | — |
| AnyTLS | 2 milestones | — |
| ShadowTLS (outbound chain) | 2 milestones | — |
| IPv6 inside the tunnel (`Ip6Config`, ULA on the TUN, changes to the IPv6 guard) | 2–3 milestones | — |
| Rule-based routing mode | 3–4 milestones | decisions on rule-sets |
| Subscriptions with background updates | 3–4 milestones | decisions on how subscriptions work |
| Import of client JSON configs and `vpn://` from AmneziaVPN | 2 milestones | — |
