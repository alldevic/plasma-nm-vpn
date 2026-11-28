# UI checklist

The manual check of the plasma-nm editors (milestone 6.9). Run it in a desktop session of the test VM with the three core packages (or a matching build) installed. After installing a newer build, restart System Settings and kded6 (or the session): a running one keeps the widget library it loaded, and an editor that needs newer symbols then fails to load there (design.md, “Editors”).

Mark each item with the date and build it passed on. Items marked “automated” also run in CI. There the editors are driven offscreen through `tests/integration/tools/editor-form.cpp`, and the widgets through the QtTest suites in `tests/unit/widgets` and `tests/unit/editors`.

Passed on 2026-11-28 with the build at commit `a8786d6` (6.8) in the local VM (Debian forky, Plasma 6, plasma-nm 6.7.4).

## Adding a profile

- [x] System Settings → Wi-Fi & Networking → “+” lists “Hysteria2”, “VLESS XHTTP Reality” and “AmneziaWG”, each with “(plasma-nm-vpn)” as its description; each opens a form on a tab “VPN (hysteria2|vless|amneziawg)”.
- [x] An empty form cannot be saved, and the problems under each block are sentences (“Server: required”, “Port: required”), never codes. Automated: every code has a rendered message (`test-blocks`), and invalid fields give readable messages (`test-*-editor`).
- [x] Keyboard: Tab moves through the fields in order, and the form scrolls to the focused field (a whole VLESS profile was entered this way).

## Secrets

- [x] The secret fields (Hysteria2 password and obfuscation password, VLESS UUID, AmneziaWG private, preshared and header protection keys) show bullets, with the eye to reveal. Automated: `test-*-editor`.
- [x] The default storage is “Store password for this user only (encrypted)” where a keychain exists (KWallet in the VM).
- [x] Stored for all users: connects from the applet without a prompt (all three protocols).
- [x] Stored for this user: entered in the editor and saved, the secret goes to KWallet and not to the system profile; the applet connects without a prompt (Hysteria2, VLESS). A secret typed at the connect prompt is not kept in the keychain: that is plasma-nm's behaviour (design.md).
- [x] Asked every time: connecting from the applet opens kded6's “VPN secrets (…) dialog” with our prompt. It asks only for what is neither stored nor not required: Hysteria2 password and obfuscation password, VLESS UUID, AmneziaWG private key alone.
- [x] Optional secrets offer “This password is not required”; left empty, they are saved as not required, and the connection starts without a prompt. Automated: `test_96_awg_editor.py`, `test_94_hysteria2_editor.py`.
- [x] VLESS “Encryption” is `none` for a new profile; an ML-KEM value is shown and saved as is. Automated: `test-vless-editor`, `test_95_vless_editor.py`.

## Kill switch

- [x] “The kill switch applies only when all traffic goes through the VPN” is shown next to the checkbox in all three editors.
- [x] AmneziaWG: without `0.0.0.0/0` in Allowed IPs the checkbox is disabled with “Allowed IPs do not include 0.0.0.0/0, so not all traffic goes through the VPN”. It follows the field as it is edited. Automated: `test-awg-editor`.
- [x] With a block left (the daemon killed after success), the form shows “Kill switch block active” with the profile name, the reason and the time. Without a block there is no banner.
- [x] “Clear” asks for administrator credentials (“Authentication is required to remove a kill switch block that cuts this computer off the network”). Cancelled, the block and the banner stay, with “The block was not cleared: authentication was refused or failed.” With the password, the chains and the `guard-status` entry are gone and so is the banner.
- [x] “Clear All” skips a profile that is connected or connecting, which stays listed as such. Automated: `test_93_banner.py`.

## Import and export

- [x] A link pasted into “Import and Export” and imported fills the form. The paste field is emptied, and “Imported, with these changes:” lists what was dropped (VLESS with `pcs`). The profile then saves and connects from the applet.
- [x] An error keeps the pasted text, and a link for another protocol names the entry to use. Automated: `test-import-widget`, `test_97_import.py`.
- [x] Importing into a form with a server or a secret asks first; declining changes nothing. Automated: `test-hysteria2-editor`.
- [x] “Open File…” imports an AmneziaWG `.conf`, and the dropped wg-quick keys and addresses are listed. Automated: `test_97_import.py`.
- [x] “Export…”: “Include secrets” is off by default, with the note on the clipboard history; the fields the format cannot carry are listed. A file with secrets is created with mode 0600. Automated: `test-export-dialog`, `test_97_import.py`.

## Saving

- [x] A new profile and a re-saved one have `vpn.persistent=yes` (all three protocols). Automated: `test_94`–`test_96`.

## plasma-nm's own import and export

- [x] “+” → “Import VPN connection…” loads every VPN plugin for the file filter, ours included: no dialog, D-Bus call or log message from them.
- [x] `.ovpn` goes to plasma-nm's OpenVPN plugin. In the VM, without `network-manager-openvpn`, it ends with “NetworkManager is missing support for OpenVPN”, as it would without our packages.
- [x] A WireGuard `.conf` is imported as a WireGuard connection.
- [x] An AmneziaWG `.conf` does not make a profile of our type. WireGuard's import rejects its AmneziaWG keys, and the file then goes to the OpenVPN plugin (which also claims `.conf`), ending with the same OpenVPN message. The README says to import it in our AmneziaWG editor (7.1).
- [x] The connection list's “Export selected connection” is disabled for our profiles (no file name is suggested): no dialog, no crash. The README says to use “Export…” in the editor (7.1).

## Observations for 7.2

- In a 1280-pixel window, System Settings gives the editor less width than its fields want, so the editor page scrolls sideways and the banner's “Clear” sits at the right edge. A maximized window or the stand-alone `kcmshell6 kcm_networkmanagement` shows it whole.
