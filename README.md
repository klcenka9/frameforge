# Frameforge

A real, experimental C++20 / Qt 6 desktop video editor for Linux. Independent code and branding inspired by professional panel-based editing workflows. GPL-3.0-or-later. **This is a first working foundation, not a Resolve replacement.**

## Install with one command (Linux Mint / Ubuntu / Debian)

```sh
curl -fsSL https://raw.githubusercontent.com/klcenka9/frameforge/v0.2.0/scripts/install.sh | bash
```

The installer uses `sudo apt-get` for distribution dependencies, builds the tagged source as your normal user, and installs to `~/.local`. Launch with `~/.local/bin/frameforge`. Target distributions: Mint 22+, Ubuntu 24.04+, Debian 12+ (Qt 6.4+ required). These source-install targets are not yet all hardware-tested. Read [the installer](scripts/install.sh) before running it if you prefer to review the commands. Do not run the whole installer with sudo.

For an existing development SDK: `bash scripts/install.sh --no-deps --source "$PWD"`. Use `--prefix /absolute/path` for another user-writable install location. The installer does not replace GPU drivers. [GitHub releases](https://github.com/klcenka9/frameforge/releases) contain the prebuilt **Debian 13 x86_64 / Qt 6.8.2** package and complete ZIP; build from source on other distributions.

[Current desktop screenshot](docs/desktop-x11.png)

## Working now

- Dockable dark desktop UI, saved dock layouts, Media/Cut/Edit/Deliver views sharing one project.
- Drag local files into the window/media pool or directly onto a timeline track; drag imported media from the pool onto the timeline. Ordered multi-file timeline drops and undo/redo are supported. Original files stay in place.
- Background video/audio import, metadata, video thumbnails, search, missing-media indicators and validated relinking.
- Play timeline / Space plays edited content and moves/scrolls the playhead, directly from the source clips, without rendering a preview file. Play source is a separate control for browsing footage.
- Two video lanes and two audio lanes; drag moves, exact frame trims/positions, split (S / Ctrl+B), deletion, edge snapping, zoom/scroll, track mute/solo/lock. Video lanes include source audio. Higher video lanes overlay lower ones; audio mixes at unity gain with peaks clamped to ±0.95 in preview and export.
- Undo/redo; versioned atomic project save/reopen; relative media paths; 30-second recovery snapshots and recovery dialog.
- New-project resolution/rate settings; CPU MP4 rendering with detected libx264 or MPEG-4 and AAC; progress, cancellation, output probing and protection against overwriting existing files or original footage.
- Runtime diagnostics distinguish compiled hardware encoders from working devices.

Fusion, Color and Fairlight are explicitly labeled planned. No node graph, grading, EQ, effects, transitions, titles, waveforms, bins, proxy workflow, render queue or hardware processing is claimed.

## Build and run

On a compatible Ubuntu/Mint development machine (package names can differ by release):

```sh
sudo apt update
sudo apt install build-essential cmake pkg-config libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev qt6-base-dev qt6-multimedia-dev libgl-dev ffmpeg gstreamer1.0-plugins-good gstreamer1.0-libav
cmake --preset dev
cmake --build --preset dev -j4
ctest --preset dev
./build/frameforge
```

In the supplied workspace a local development SDK was extracted without installing system packages. Exact commands:

```sh
export PKG_CONFIG_SYSROOT_DIR=/workspace/.sdk/root
export PKG_CONFIG_LIBDIR=/workspace/.sdk/root/usr/lib/x86_64-linux-gnu/pkgconfig
export LD_LIBRARY_PATH=/workspace/.sdk/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
/workspace/.sdk/root/usr/bin/cmake -S /workspace/github-frameforge -B /workspace/build-live -DCMAKE_PREFIX_PATH=/workspace/.sdk/root/usr
/workspace/.sdk/root/usr/bin/cmake --build /workspace/build-live -j4
/workspace/.sdk/root/usr/bin/ctest --test-dir /workspace/build-live --output-on-failure
/workspace/build-live/frameforge
```

A graphical session is required for interactive use. The cloud environment has no real display/audio server. Tests use Qt's offscreen platform; sandbox audio initialization requires the runtime's network permission even with no PulseAudio server. This is an environment restriction, not a requirement for network access when editing normally.

## First edit

1. Set File → Project settings before import, if needed. Import a local video with Ctrl+I.
2. Drop files onto the timeline to import and place them, or drag imported media from the pool. Double-click or Ctrl+Return still append to the selected compatible track.
3. Select a timeline clip. Click the ruler to position the playhead, S or Ctrl+B to split, drag to move, or apply exact frame values in Inspector. Delete removes the selected clip. Ctrl+Z undoes.
4. Click **Play timeline / Pause** or press Space to play edits from the playhead. Playback reads the active source clips directly; edits and scrubbing never encode a preview. Editing pauses at the playhead; press Space to resume. **Play source / Pause** browses the selected source separately.
5. Save with Ctrl+S. Open Deliver and export to a new MP4 filename. Rendering uses a snapshot; later edits do not change an export already running.

See [architecture](docs/ARCHITECTURE.md), [build/packaging](docs/BUILDING.md), [project/recovery format](docs/PROJECT_FORMAT.md), [GPU support](docs/GPU_SUPPORT.md), [test evidence](docs/TESTING.md), and [roadmap](docs/ROADMAP.md).
