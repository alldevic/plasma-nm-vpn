#!/bin/bash
# vm.sh up <image> | ssh [command...] | copy <dir> <guest dir> | wait | down
# The test VM under KVM, reached only over AF_VSOCK (never over IP, so that tests may cut the guest's
# networking). State lives in $PNV_VM_DIR (default ./vm-state).
set -euo pipefail

dir=${PNV_VM_DIR:-$PWD/vm-state}
cid=${PNV_VM_CID:-3}
key=$dir/key

ssh_vm() {
  ssh -q -o BatchMode=yes -o ConnectTimeout=10 -o ServerAliveInterval=10 -o ServerAliveCountMax=6 \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR \
    -o "ProxyCommand=socat - VSOCK-CONNECT:$cid:22" -i "$key" root@vm "$@"
}

wait_vm() { # until ssh answers; a suspended VM is asleep, not dead
  for _ in $(seq "${1:-120}"); do
    ssh_vm true 2>/dev/null && return 0
    kill -0 "$(cat "$dir/qemu.pid")" 2>/dev/null || { echo "vm.sh: QEMU exited" >&2; tail -20 "$dir/console.log" >&2; return 1; }
    sleep 2
  done
  echo "vm.sh: no ssh over vsock" >&2
  tail -5 "$dir/console.log" >&2
  ls -l /dev/vhost-vsock /dev/vsock >&2 || true
  socat -V 2>&1 | head -2 >&2
  ssh_vm -v true 2>&1 | tail -15 >&2
  return 1
}

case ${1:-} in
  up)
    image=$(readlink -f "$2")
    mkdir -p "$dir"
    [ -f "$key" ] || ssh-keygen -q -t ed25519 -N '' -f "$key"
    qemu-img create -q -f qcow2 -b "$image" -F qcow2 "$dir/disk.qcow2"
    # S3 enabled for the sleep test (0.2); the ssh key arrives as a systemd credential, which
    # sshd@.service (sshd on vsock, from systemd-ssh-generator) reads as an AuthorizedKeysFile.
    qemu-system-x86_64 -enable-kvm -machine q35 -cpu host -m 3072 -smp 2 \
      -global ICH9-LPC.disable_s3=0 -display none -serial "file:$dir/console.log" \
      -drive file="$dir/disk.qcow2",if=virtio \
      -nic user,model=virtio-net-pci,mac=52:54:00:12:34:56 \
      -device vhost-vsock-pci,guest-cid="$cid" \
      -smbios "type=11,value=io.systemd.credential.binary:ssh.ephemeral-authorized_keys-all=$(base64 -w0 "$key.pub")" \
      -daemonize -pidfile "$dir/qemu.pid"
    wait_vm
    ;;
  ssh) shift; ssh_vm "$@" ;;
  copy) tar -C "$2" -cf - . | ssh_vm "mkdir -p '$3' && tar -C '$3' -xf -" ;;
  wait) wait_vm "${2:-120}" ;;
  down)
    ssh_vm poweroff 2>/dev/null || true
    pid=$(cat "$dir/qemu.pid" 2>/dev/null || true)
    for _ in $(seq 30); do [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null || break; sleep 1; done
    [ -n "$pid" ] && kill "$pid" 2>/dev/null || true
    ;;
  *) echo "usage: vm.sh up <image> | ssh [cmd] | copy <dir> <guest dir> | wait | down" >&2; exit 2 ;;
esac
