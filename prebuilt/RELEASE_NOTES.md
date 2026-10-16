Frameforge 0.1.0 — experimental native Linux video editor

Install from source on Linux Mint 22+, Ubuntu 24.04+ or Debian 12+ (Qt 6.4+):

```sh
curl -fsSL https://raw.githubusercontent.com/klcenka9/frameforge/v0.1.0/scripts/install.sh | bash
```

The installer uses sudo apt-get for distribution dependencies, builds as your normal user, and installs to ~/.local. Launch with ~/.local/bin/frameforge. Do not run the whole installer with sudo.

Included: source import/playback, frame-based multitrack editing, split/trim/move/delete, undo/redo, project persistence/recovery, and background CPU timeline preview/export.

Assets:
- Complete project ZIP, including a built executable, tests and documentation.
- Debian package built on Debian 13 x86_64 with Qt 6.8.2; not a universal Mint binary.
- Source archive matching this tag and SHA256 checksums.

Verification: four CTest suites and Xvfb X11 editing/playback passed on Debian 13. The source installer completed a real Release build and user-prefix installation with existing dependencies. Clean-machine Mint/Ubuntu installation and physical GPU/audio hardware remain unverified. Advanced workspaces are planned; edited timeline playback requires a background preview render.
