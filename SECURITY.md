# Security

## Reporting a vulnerability

Report vulnerabilities privately through GitHub:
[Security → Report a vulnerability](https://github.com/alldevic/plasma-nm-vpn/security/advisories/new).
Do not open public issues for them. Reports in English or Russian are welcome. You will get an answer within a week;
a fix is released as a new version, and the advisory is published with it.

## What counts

The VPN daemons run as root, started by NetworkManager, and they read profiles that an unprivileged user may create.
So we treat these as vulnerabilities:

- anything that lets a profile, an imported link or `.conf`, or data from a VPN server make the daemons or the cores
  run code, read or write files, or change the system beyond the connection itself (privilege escalation);
- secrets (passwords, UUIDs, keys) reaching places other users can read: logs, `vpn.data`, files with wide
  permissions;
- traffic or DNS leaving past the tunnel when the kill switch says it should not, or IPv6 or DNS leaks with a tunnel
  that routes all traffic;
- a way for an unprivileged user to clear another profile's kill switch block without administrator credentials.

Bugs in the cores themselves (sing-box, Xray, amneziawg-go, awg) belong to their projects; tell us too if our
packages ship an affected version.

## Supported versions

Only the latest release gets fixes.
