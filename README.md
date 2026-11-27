# plasma-nm-vpn

NetworkManager VPN plugins and KDE Plasma editors for modern censorship-resistant protocols:
Hysteria2, VLESS XHTTP Reality and AmneziaWG, with a kill switch. Target: Debian forky with KDE Plasma 6, amd64.

**Status: in development.** Nothing is ready for use yet. The design and the plan are in [`docs/`](docs/).

## Build

Build dependencies on forky: `build-essential cmake pkgconf libnm-dev libglib2.0-dev libjson-glib-dev`
and, for the editors, `qt6-base-dev extra-cmake-modules libkf6coreaddons-dev libkf6widgetsaddons-dev
libkf6i18n-dev libkf6networkmanagerqt-dev plasma-nm`.

```
cmake -B build && cmake --build build && ctest --test-dir build
```

Installing into `/usr` needs `-DCMAKE_INSTALL_PREFIX=/usr -DKDE_INSTALL_USE_QT_SYS_PATHS=ON`, so that
the editors land in Qt's plugin directory.

## Packages

The cores are pinned in [`cores/`](cores/); `scripts/fetch-cores` downloads or builds them and checks every hash
(it needs network access, Go, git, curl and unzip). Then the build itself runs offline:

```
scripts/fetch-cores cores-staging
dpkg-buildpackage -b -us -uc
```

## License

MIT, see [`LICENSE`](LICENSE). The plasma-nm headers in [`third_party/plasma-nm/`](third_party/plasma-nm/)
are LGPL; the editors link against plasma-nm's editor library.
