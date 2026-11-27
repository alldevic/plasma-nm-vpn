# 0.8 notes

Checked on 2026-11-27 in the VM (plasma-nm 4:6.7.4-2+b1, Qt 6.11.2, NM 1.58.1), with no `pnv-proto.name` in `/usr/lib/NetworkManager/VPN/` (removed, NM restarted).

## Build

```
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr -DKDE_INSTALL_USE_QT_SYS_PATHS=ON
cmake --build build && sudo cmake --install build
```

- `libplasmanm_editor.so` is unversioned: `/usr/lib/x86_64-linux-gnu/libplasmanm_editor.so` (from `plasma-nm`, no dev package needed).
- ECM: `set(QT_MAJOR_VERSION 6)` before `find_package(ECM)` and `include(KDEInstallDirs6)`; plain `KDEInstallDirs` or a missing `QT_MAJOR_VERSION` looks for Qt 5's qmake and fails.
- Without `KDE_INSTALL_USE_QT_SYS_PATHS=ON` the module goes to `/usr/lib/x86_64-linux-gnu/plugins/…`, which Qt does not search; with it, to `/usr/lib/x86_64-linux-gnu/qt6/plugins/plasma/network/vpn/`. The Debian package build sets this.
- Every undefined plasma-nm symbol of the module (`VpnUiPlugin` and `SettingWidget`: constructors, destructors, metaobject, `loadConfig`, `loadSecrets`, `watchChangedSetting`, `importConnectionSettings`, `exportConnectionSettings`, `suggestedAuthDialogButtons`, typeinfo) resolves from `libplasmanm_editor.so`. The other libraries in `ldd` are that library's own dependencies.
- The build is warning-free with `KDECompilerSettings`.

## In System Settings (driven through QEMU input events and screenshots)

- “Connections → +” lists “PNV proto / Prototype editor (milestone 0.8)” among the VPN types, with no `.name` file: the list comes from the plugin metadata alone.
- Selecting it opens the editor with a tab “VPN (pnv-proto)”: plasma-nm labels the tab with the last component of the service name, so the product names give “VPN (hysteria2)”, “VPN (vless)”, “VPN (amneziawg)”.
- Saving works without `.name`: `nmcli` shows `vpn.service-type org.freedesktop.NetworkManager.pnv-proto`, `vpn.data server = srv.stand.test`. After restarting System Settings the field shows the saved value.
- With `LANG=ru_RU.UTF-8 LANGUAGE=ru` (after `locale-gen`), the entry reads “PNV прототип / Прототип редактора (веха 0.8)”.
- The type list ends with plasma-nm's own “Import VPN connection…”; it was not tried here (plasma-nm's import is not supported, design.md).
