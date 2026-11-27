# 0.9 notes

Rehearsed on 2026-11-27 against `alldevic/plasma-nm-vpn-test` with apt 3.3.7 (forky) in the VM.

## Test key

ed25519, signing only, valid 2 years, user ID “plasma-nm-vpn TEST repository key (rehearsals only, not for production)”, fingerprint `50C0E22E388A3E4540E4997BE846F285ECEFDFAE`. Public keyring: `plasma-nm-vpn-test.gpg` here. The private part is kept outside the repository (on the developer's machine, in a separate `GNUPGHOME`) for the 1.3 rehearsals.

## Releases in the test repository

| Tag | State | Contents |
|---|---|---|
| v0.0.1 | published | `pnv-apt-test 0.1.0+sb1.14.4`, `pnv-apt-test-common 0.1.0`, `pnv-apt-test-rc 0.1.0~rc1` |
| v0.0.2 | published | `pnv-apt-test 0.1.1+sb1.14.4`; the other two carried over from v0.0.1 (sha256 equal) |
| v0.0.3 | draft, never published | only `Packages` and `InRelease`; clients kept seeing v0.0.2 |
| v0.0.4 | published, then deleted | `InRelease` signed by a throwaway key: apt refused it (“Missing key …” from `sqv`) |
| v0.0.5 | published, latest | v0.0.2's packages, newly signed index |

## Commands

```
GNUPGHOME=<test key> proto/0.9/make-repo alldevic/plasma-nm-vpn-test <outdir> <version> [<previous tag>]
gh release create <tag> --draft … *.deb Packages Packages.xz InRelease
gh release view <tag> --json assets      # names as uploaded
gh release edit <tag> --draft=false
```

## Findings

- GitHub renames `~` to `.` and keeps `+`. Rule: both become `-` in asset names.
- apt follows `latest/download/./<file>` → `download/<tag>/<file>` → `release-assets.githubusercontent.com` without warnings.
- With `Packages` and `Packages.xz` both published, apt fetches `.xz`; with only `Packages` it gets a 404 for `.xz` and uses `Packages`, no warning (checked against a local copy served over HTTP).
- `apt-ftparchive packages .` prefixes `Filename:` with `./` and scans subdirectories; `apt-ftparchive release .` lists a `Release` that exists in the directory, including the one being written. The script builds in a temporary directory and writes `Release` outside.
- Carry-over must compare `Package:` and `Version:` literally: versions contain `+` and `.`, which a regular expression misreads.
- GitHub caches the `latest/download` redirect per file and per `Accept` header (`Vary: Accept`; apt sends `Accept: text/*`, curl `*/*`). For about two minutes after publishing v0.0.2, apt was redirected to v0.0.1 for `InRelease`, and then got v0.0.2's `InRelease` with v0.0.1's `Packages.xz`: one “Hash Sum mismatch”, gone on the next run.
- After v0.0.4 (bad signature) was deleted, and even after v0.0.5 was published, the VM kept failing: apt keeps the rejected `InRelease` in `lists/partial/`, resumes it with `Range: bytes=1162-` and `If-Range`, and v0.0.5's `InRelease` has the same size, so the server answers 416 and apt re-checks the stale file. `rm /var/lib/apt/lists/partial/*plasma-nm-vpn-test*` fixed it at once.
