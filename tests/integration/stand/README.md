# Test bench

Servers for the MVP protocols and the network around them, in the test VM (or CI). Never on a work machine: `stand-up` takes over the default route and DNS.

```
scripts/fetch-cores <dir> && sudo install -D -t /opt/pnv-proto/bin <dir>/bin/*   # the cores pinned in cores/
sudo ./stand-up [hy2|vless|awg|all]
sudo ./stand-down
```

## Topology

| What | Where | Address |
|---|---|---|
| `stand-cli`, the client's "physical" link, NM profile `stand` | root namespace | 192.0.2.2/24, 2001:db8:1::2/64, LAN 192.168.77.2/24 |
| gateway, "ISP" DNS :53 | netns `stand`, `stand-srv` | 192.0.2.1, 2001:db8:1::1 |
| LAN neighbour, HTTP :80 | netns `stand`, `stand-srv` | 192.168.77.1 |
| server: hysteria2 UDP 443 (Salamander) and 4443 (no obfuscation, 198.51.100.1 only, 6.3), VLESS TCP 443, AmneziaWG UDP 51820 | netns `stand`, `lo` | 198.51.100.1, 2001:db8:100::1 |
| "internet": HTTP :80, DNS :53 | netns `stand`, dummy `inet0` | 203.0.113.1, 2001:db8:113::1; DNS also 1.1.1.1 |
| AmneziaWG tunnel | server / client `-4` / client `-6` | 100.64.0.1/24 / 100.64.0.2 / 100.64.0.3 |

Port hopping: UDP 20000–20100 on 198.51.100.1 is DNATed to the hysteria2 port 443 (netns `stand`, `table inet stand`, chain `hop`, with a counter).

The "internet" is reachable only through a tunnel: nftables in the netns drops traffic to it arriving on `stand-srv`, so direct traffic there is a leak. Capture leaks on `stand-cli`.

The management connection (the VM's NIC) is the one deliberate exception to these addresses: `stand-up` keeps its address but sets `ipv4.never-default`, `ipv4.ignore-auto-dns` and `ipv6.method disabled`; `stand-down` restores the saved values. `stand-cli` is managed via `/etc/NetworkManager/conf.d/90-pnv-stand.conf` (veth is unmanaged by default), so it survives an NM restart.

## Names

| Name | "ISP" DNS 192.0.2.1 | Tunnel DNS 203.0.113.1, 1.1.1.1 |
|---|---|---|
| `srv.stand.test` | A 198.51.100.1, AAAA 2001:db8:100::1 | same |
| `srv6.stand.test` | AAAA 2001:db8:100::1 only | same |
| `stand.test` | no address | A 203.0.113.1, AAAA 2001:db8:113::1 |
| `.` SOA (liveness check) | — | NOERROR with an answer |

Both are dnsmasq instances with `--auth-zone`; the root zone answers only as an auth zone of its own, and `stand.test` needs a separate one.

## Secrets and client configs

Generated on the first `stand-up` into `secrets/` (git-ignored): the hysteria2 certificate for both server names, the Reality keys, UUID, passwords and AWG keys. Client configs for manual tests are in `secrets/client/`: `-4` connects to 198.51.100.1, `-6` to `srv6.stand.test` (AAAA only), so each really uses its address family:

```
sudo sing-box run -c secrets/client/hy2-4.json      # SOCKS 127.0.0.1:1080 (-6: 1081)
sudo xray run -c secrets/client/vless-4.json        # SOCKS 127.0.0.1:1082 (-6: 1083)
curl --socks5-hostname 127.0.0.1:1080 http://stand.test/

sudo amneziawg-go awg-cli && sudo awg setconf awg-cli secrets/client/awg-4.conf
sudo ip addr add 100.64.0.2/24 dev awg-cli && sudo ip link set awg-cli up
sudo ip route add 203.0.113.0/24 dev awg-cli && curl http://203.0.113.1/
```

## Bench details that matter

- The netns has default routes into `inet0`, which lead nowhere: sing-box refuses to dial without a default interface.
- Routes to the client network use the server address as source; otherwise amneziawg-go replies from 192.0.2.1 and the client roams its endpoint to the gateway.
- `stand-up` waits for IPv6 DAD on `stand-cli`: NM reports the profile up earlier, and the first packets would leave from `::1`.
- xray's `freedom` blocks private and reserved targets behind server inbounds by default; the server config allows the bench "internet". Client-side inbounds are not affected.
- The Reality target is `tls-target`, a threaded TLS 1.3 server: `openssl s_server` takes seconds for the first handshake and serves one connection at a time.
- AWG has one peer per address family (`-4` and `-6` keys): when one peer switches family, amneziawg-go loses its first handshake response and the client waits 5 s for the retry. Rarely (about 1 in 15 runs) the first handshake still takes 5 s; tests allow for one retry.
- AWG uses 3.1 parameters. With `HeaderProtectionKey`, S1–S4 must be at least 12; Jmin must not exceed Jmax.
- Tools: iproute2, nftables, NetworkManager, dnsmasq-base, python3, openssl, curl, bind9-dnsutils; `scripts/fetch-cores` (run where the cores are built) needs git, make, gcc, unzip and golang-go.
