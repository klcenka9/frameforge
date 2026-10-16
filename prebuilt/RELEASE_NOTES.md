Frameforge 0.1.1 — Ubuntu playback dependency fix

One-command source installation (Mint 22+, Ubuntu 24.04+, Debian 12+):

```sh
curl -fsSL https://raw.githubusercontent.com/klcenka9/frameforge/v0.1.1/scripts/install.sh | bash
```

Installs distribution build/runtime dependencies using sudo apt-get, builds as your normal user, and installs under ~/.local. Launch: ~/.local/bin/frameforge. Do not run the entire installer as root.

This patch includes GStreamer container/codec plugins required by Ubuntu Qt 6.4 for MP4 preview playback. Ubuntu 24.04 / Qt 6.4.2 and Debian 13 / Qt 6.8.2 both passed all four CTest suites. Physical Mint/GPU hardware is still unverified.

Assets: complete project ZIP with built application and tests; Debian 13 x86_64 / Qt 6.8.2 package; tagged source archive; SHA256 checksums. The Debian binary is not a universal Mint binary; use the source installer on other supported distributions.

Frameforge remains experimental: background render is required for edited timeline playback, and compositing/grading/professional audio workspaces remain planned.
