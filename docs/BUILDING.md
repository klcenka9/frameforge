# Build and packaging

Requirements: C++20 compiler, CMake 3.22+, Qt 6.4+ Core/Gui/Widgets/Concurrent/Multimedia/MultimediaWidgets/Test, FFmpeg and FFprobe on PATH. Qt is dynamically linked; FFmpeg runs as an external process. No external source is vendored into the application. Build/test commands are in README.

Debian 13 / Qt 6.8.2 / GCC 14 is the current build environment. Mint/Ubuntu compilation is a target, not yet certified. Qt runtime multimedia codec/plugin availability varies by distribution. CTest generates its own small media; no footage is downloaded.

## Native install / uninstall

```sh
cmake --install build --prefix "$HOME/.local"
# uninstall this per-user installation:
rm "$HOME/.local/bin/frameforge"
rm "$HOME/.local/share/applications/frameforge.desktop"
rm -r "$HOME/.local/share/doc/frameforge"
```

Projects and imported footage are never removed by uninstall. Preferences and recovery live under Qt's standard user config/data paths for Frameforge.

## Debian package

```sh
cmake --preset release
cmake --build --preset release -j4
ctest --preset release
(umask 022; cd build-release && cpack -G DEB)
sudo apt install ./build-release/Frameforge-0.1.1-Linux.deb
sudo apt remove frameforge
```

Build packages on the oldest target distribution to avoid glibc/Qt symbol incompatibilities. The Debian 13 package is a Debian 13 artifact, not a portable Mint binary. Inspect `dpkg-deb -I` and `dpkg-deb -c` before installation. Use umask 022 for CPack so newly staged parent directories are readable/executable by all users. Dependencies are distribution packages; no restricted codec or proprietary driver is bundled. Review FFmpeg's actual license/configuration on each release platform; this development FFmpeg enables GPL components. The application's GPL-3.0-or-later license permits the selected dynamically linked Qt modules under their applicable open-source terms. Third-party notices remain with system packages. Flatpak is deferred pending multimedia and GPU integration validation.

## Troubleshooting

- Missing Qt CMake packages: install Qt development packages and match their Qt runtime versions.
- Missing FFmpeg/FFprobe or AAC/video encoders: install your distribution's supported multimedia package; inspect Help → Backend diagnostics. Encoder names alone do not verify hardware.
- Black viewer: check that the local source exists, select it again, inspect playback error in status bar; timeline edits require Render timeline preview.
- Audio without an output device: preview video and export remain useful; actual speaker playback needs a working PipeWire/PulseAudio setup. The test environment has none.
- Export fails: error dialog includes FFmpeg diagnostics. Existing outputs are refused. Use a writable directory and new filename; missing-media errors require relinking.
- X11 fallback: `QT_QPA_PLATFORM=xcb ./build/frameforge`; Wayland: `QT_QPA_PLATFORM=wayland ./build/frameforge` if the relevant Qt platform plugin is installed. Neither implies a tested real desktop configuration.
- CPU decoding for Qt's FFmpeg backend: `QT_FFMPEG_DECODING_HW_DEVICE_TYPES=, ./build/frameforge` (Qt 6.8 tested). This is a Qt backend debug setting and may vary by version. Export always uses software decoding/encoding here.

For Qt builds using GStreamer (including Ubuntu 24.04 Qt 6.4), install `gstreamer1.0-plugins-good gstreamer1.0-libav` for MP4/MPEG-4/H.264/AAC playback. The one-command installer includes these packages.
