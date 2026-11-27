#!/bin/bash
# run.sh --dns-mode <resolvconf|resolved> --image <qcow2> --cores <dir> --debs <dir> --test-build <dir>
# Bring up the test VM, copy the source tree, the cores, the .deb files and the test build (a DESTDIR tree) in,
# install the packages, start the bench, run all cases with pytest
# and collect the report into $PNV_OUT (default ./integration-out); on failure also the VM's journal,
# connections, ruleset, addresses and routes. Exit code: that of pytest.
set -euo pipefail

here=$(dirname "$(readlink -f "$0")")
root=$(readlink -f "$here/../..")
out=${PNV_OUT:-$PWD/integration-out}
vm=$here/vm.sh

while [ $# -gt 0 ]; do
  case $1 in
    --dns-mode) mode=$2 ;;
    --image) image=$2 ;;
    --cores) cores=$(readlink -f "$2") ;;
    --debs) debs=$(readlink -f "$2") ;;
    --test-build) testbuild=$(readlink -f "$2") ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
  shift 2
done
mkdir -p "$out"

t0=$(date +%s)
trap '"$vm" down' EXIT
"$vm" up "$image"
echo "VM up in $(( $(date +%s) - t0 )) s"

tar -C "$root" --exclude=.git --exclude=./build --exclude=./cores-staging --exclude=./vm-state \
  --exclude=./integration-out --exclude=./debs --exclude=./test-build --exclude='./*.qcow2' -cf - . |
  "$vm" ssh "mkdir -p /root/src && tar -C /root/src -xf -"
"$vm" copy "$cores" /root/cores
"$vm" copy "$debs" /root/debs
"$vm" copy "$testbuild" /root/test-build
"$vm" ssh /root/src/tests/integration/guest/prepare /root/cores /root/debs
# A capture on the bench's client link for the whole run, kept on failure.
"$vm" ssh "systemd-run --quiet --unit=pnv-capture tcpdump -n -i stand-cli -s 256 -w /root/stand-cli.pcap"

# The harness must survive the guest dropping all IP output: it goes over vsock.
"$vm" ssh "nft add table inet harness-check && nft add chain inet harness-check out '{ type filter hook output priority 0; policy drop; }' && nft add rule inet harness-check out oifname lo accept"
"$vm" ssh true && echo "vsock session survives a drop-all output rule"
"$vm" ssh "nft delete table inet harness-check"

# The cases run detached in the guest: suspend (and later tests that cut the guest's networking) drops
# the vsock session, so the harness reconnects and waits for the exit code instead of holding a session.
"$vm" ssh "systemd-run --quiet --unit=pnv-cases --working-directory=/root/src/tests/integration \
  -E PNV_DNS_MODE=$mode -E PNV_KVM=1 -E PNV_TEST_BUILD=1 -E PNV_TEST_BUILD_DIR=/root/test-build \
  -E PNV_PACKAGES=1 sh -c 'python3 -m pytest -v -p no:cacheprovider \
  --junitxml=/root/report.xml cases > /root/cases.log 2>&1; echo \$? > /root/cases.rc'"
for _ in $(seq 360); do
  sleep 5
  "$vm" wait 60 && "$vm" ssh test -f /root/cases.rc 2>/dev/null && break
done
"$vm" ssh cat /root/cases.log || true
rc=$("$vm" ssh cat /root/cases.rc 2>/dev/null || echo 1)
"$vm" ssh cat /root/report.xml > "$out/report.xml" || true

if [ $rc != 0 ]; then
  "$vm" ssh 'journalctl -b --no-pager' > "$out/journal.txt" || true
  "$vm" ssh 'nmcli -f all connection show; nmcli device' > "$out/nmcli.txt" || true
  "$vm" ssh 'nft list ruleset' > "$out/nft.txt" || true
  "$vm" ssh 'ip addr; ip -4 route; ip -6 route; ip netns exec stand ip addr' > "$out/ip.txt" || true
  "$vm" ssh 'systemctl stop pnv-capture; cat /root/stand-cli.pcap' > "$out/stand-cli.pcap" || true
fi
echo "total $(( $(date +%s) - t0 )) s"
exit $rc
