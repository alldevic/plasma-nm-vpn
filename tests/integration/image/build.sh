#!/bin/bash
# build.sh <resolvconf|resolved> <out.qcow2>: the test VM image, a Debian forky cloud image (generic kernel, so
# mac80211_hwsim exists) configured by cloud-init on one boot under KVM. resolvconf: Debian's default, NM writes
# /etc/resolv.conf; resolved: systemd-resolved installed. Needs qemu-system-x86, qemu-utils, cloud-image-utils.
set -euo pipefail

mode=$1 out=$(readlink -f "$2")
here=$(dirname "$(readlink -f "$0")")
base=https://cloud.debian.org/images/cloud/forky/daily/latest
img=debian-14-generic-amd64-daily.qcow2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"

curl -fsSL -o "$img" "$base/$img"
curl -fsSL -o SHA512SUMS "$base/SHA512SUMS"
grep " $img\$" SHA512SUMS | sha512sum -c --quiet -

case $mode in
  resolvconf) purge=systemd-resolved\ libnss-resolve resolv=/etc/resolv.conf ;;
  resolved) purge= resolv= ;;
  *) echo "mode must be resolvconf or resolved" >&2; exit 2 ;;
esac
sed -e "s|@PURGE_RESOLVED@|$purge|" -e "s|@RESOLV_CONF@|$resolv|" "$here/user-data.in" > user-data
[ "$mode" = resolved ] && sed -i 's|^  - pkexec$|  - pkexec\n  - systemd-resolved|' user-data
cloud-localds seed.iso user-data

qemu-img resize -q "$img" 8G
timeout 30m qemu-system-x86_64 -enable-kvm -machine q35 -cpu host -m 2048 -smp 2 \
  -display none -serial file:console.log \
  -drive file="$img",if=virtio -drive file=seed.iso,media=cdrom,readonly=on \
  -nic user,model=virtio-net-pci || { tail -30 console.log >&2; exit 1; }
grep -q "Cloud-init.*finished" console.log || { tail -30 console.log >&2; echo "cloud-init did not finish" >&2; exit 1; }
qemu-img convert -c -O qcow2 "$img" "$out"
