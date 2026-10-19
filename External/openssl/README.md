# OpenSSL

| Folder | Used by | What |
|---|---|---|
| `openssl-3.5.9/` | Windows x64: MutedVision, client, game server, lobby server, `Source/MV/Tests`, `Source/MV/Bench` | OpenSSL 3.5.9 LTS (supported until 2030-04-08), static libraries |
| `openssl-1.1.1g/` | Android, iOS, macOS projects | Prebuilt 1.1.1g (out of support); not rebuilt here |

## Windows x64 (`openssl-3.5.9/`)

- Source: `https://github.com/openssl/openssl/releases/download/openssl-3.5.9/openssl-3.5.9.tar.gz`
- SHA256: `603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a`
- Signed by the OpenSSL signing certificate, primary key `B146 647E 45A7 B339 47AB 226B 2A2C 87D1 6169 2D40`
  (keys: `https://openssl-library.org/source/pubkeys.asc`).
- Configure: `perl Configure VC-WIN64A no-shared no-tests` (assembly through NASM), built with MSVC from Visual Studio 18.
- Layout: `include/openssl/`, `x64/lib/libssl.lib`, `x64/lib/libcrypto.lib`, and `x64/bin/openssl.exe` (static; left
  untracked by the `*.exe` rule).
- The libraries are compiled `/MT /Zl` without debug info (the script strips OpenSSL's default `/Zi`, which would be
  most of `libcrypto.lib`'s size): no CRT is named in them, so the `/MD` Release and Shipping builds and the `/MDd` Debug
  build all link the same files, and crash stacks show no OpenSSL function names. Nothing loads an OpenSSL DLL.
- Linking needs `libssl.lib libcrypto.lib ws2_32.lib gdi32.lib advapi32.lib crypt32.lib user32.lib`.
- Compiled-in paths are OpenSSL's Windows defaults (`OPENSSLDIR` = `C:\Program Files\Common Files\SSL`, modules under
  `C:\Program Files\OpenSSL`), which only administrators can write.

## Rebuilding or upgrading

`build_openssl_windows.bat` downloads the release, checks its SHA256 against the published `.sha256`, checks the PGP
signature against `OPENSSL_SIGNING_FINGERPRINT` when `gpg` is found (Git for Windows ships one), builds, and replaces
`openssl-<version>/`. It needs a native Windows Perl (Strawberry Perl; the MSYS perl in Git for Windows does not work)
and NASM; `OPENSSL_PERL`, `OPENSSL_NASM`, `OPENSSL_WORK` and `VSDEVCMD` override where they are found. Run it from a
plain `cmd` prompt (or `cmd /c call build_openssl_windows.bat`); it takes about 10 minutes.

To move to another release:

1. Set `OPENSSL_VERSION` at the top of the script (and `OPENSSL_SIGNING_FINGERPRINT` if the release page names a new
   signing key), then run it.
2. Replace `openssl-<old>` with `openssl-<new>` in the Windows `.vcxproj` files under `VSProjects/`,
   `Source/MV/Tests/build_tests.bat` and `Source/MV/Bench/build_*.bat`, and in this file.
3. `git rm -r` the old folder, rebuild, and run the `MV OpenSSL` suite in
   `mv_tests` (it prints the linked version and performs a TLS 1.3 handshake over loopback).
