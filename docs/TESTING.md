# Verification — 2026-10-16

Environment: Debian GNU/Linux 13, GCC 14.2, CMake 3.31.6, Qt 6.8.2, FFmpeg/FFprobe 7.1.5. Qt development/CMake packages were extracted under `.sdk` because the managed filesystem does not permit system installation. There was no existing repository or application to preserve; `/workspace/.git` is a read-only managed mount, not an initialized working repository, so no commits were created.

## Results

- CMake configure and full native C++ build: passed.
- CTest: **4/4 suites passed**, 5.11 seconds on the final functional run.
- Core: 8 Qt Test results passed, including lifecycle hooks. Edits, source bounds, overlap rejection, locks, relative-path save/reopen, missing sources, corrupt project rejection, source-overwrite prevention and extreme rational arithmetic.
- Media: generated video/tone, Unicode/quoted filename, unsupported/corrupt file rejection, CPU export, output metadata/duration, decoded red/black/blue pixels, overlay end/reveal behavior, PCM silence/tone alignment, muted track graph and cancellation cleanup.
- Desktop offscreen at 1× and 2×: asynchronous import and thumbnails, valid decoded source frames, actual undo/redo keyboard shortcuts, split/move/trim, relinking moved footage, save/reopen into the view, dirty-project recovery snapshot and decoded rendered-timeline preview.
- X11/Xvfb: same desktop workflow passed, 3 Qt Test results (including hooks), 1.918 seconds. Software decoder selected. A real X11 capture is in [desktop-x11.png](desktop-x11.png).
- CPack Debian package: generated and inspected for expected binary/desktop entry/license/readme, runtime dependencies, readable 0755 directories and absence of a workspace RPATH. Installation on a clean machine has not been tested.

The first desktop test timed out in the restricted sandbox's PulseAudio initialization; granting the execution runtime's network access resolved that environment issue. Tests use a nonexistent PulseAudio socket and have no speaker device. Xvfb has no window manager, so the shortcut test explicitly establishes Qt's active window (this test-only Qt API produces a deprecation warning on Qt 6.8). Expected multimedia warnings include no audio output device, unavailable VDPAU and no hardware decoder. Rendered audio is checked by decoding PCM; audible speaker playback is unverified.

Safety review found and fixed an overwrite guard missing from direct core saves and an integer overflow in extreme-rate frame conversion. Both regression tests were observed failing before the fixes and pass now. Package staging initially inherited umask 077; clean staging with umask 022 produced correct directory permissions. A shortcut harness failure under window-manager-free Xvfb was resolved by explicit test activation.

## Reproduce

```sh
cmake --preset dev
cmake --build --preset dev -j4
ctest --preset dev
# optional, with Xvfb installed:
./scripts/test-x11.sh
```

In this workspace, use the local SDK commands in README and run graphical tests with the tool runtime's network permission. `scripts/test-x11.sh` accepts `XVFB_BINARY`; the local path is `/workspace/.sdk/root/usr/bin/Xvfb`. CTest logs are in `build/Testing/Temporary/LastTest.log`, and X11 results in `build/x11-test.txt`.

## Not verified

Mint builds/installs; clean-machine Ubuntu interactive installation; real Wayland/Cinnamon/GNOME; physical NVIDIA/Intel/hybrid devices or failed GPU initialization; multi-monitor switching; actual audio output; long-running or VFR footage; arbitrary source stream offsets/rotation; 4K/8K performance; memory/disk growth over hours; abrupt-process recovery; disk-full handling; 10,000-clip UI performance. No corresponding compatibility or performance claim is made. A single recovery slot, 64-clip render limit, session-local undo, and render-before-play timeline previews are current product limits.

## Ubuntu 24.04 regression verification (0.1.1)

GitHub Actions run https://github.com/klcenka9/frameforge/actions/runs/38961945194 passed configure, build and all four CTest suites on Ubuntu 24.04 / Qt 6.4.2. The initial run caught missing GStreamer MP4 decoding; installing gstreamer1.0-plugins-good and gstreamer1.0-libav fixed it without weakening the desktop tests. Debian 13 Release 0.1.1 also passed 4/4 suites in 4.87 seconds. The public v0.1.0 installer was downloaded and completed a real source-download/build/install with existing dependencies; v0.1.1 adds the verified Ubuntu multimedia dependencies.

## Playback and drop regression verification (0.1.2)

Debian 13 / Qt 6.8.2: full build and 4/4 CTest suites passed in 16.70 seconds. Native X11/Xvfb desktop test: 7 Qt Test results passed in 8.078 seconds. Ubuntu 24.04 / Qt 6.4.2 / GStreamer: configure/build and 4/4 suites passed at https://github.com/klcenka9/frameforge/actions/runs/39013508445 . Desktop suites run with G_DEBUG=fatal-criticals; no GLib critical was produced in these fixtures.

New coverage exercises Play timeline and moving playhead, pause/resume/seek, separate source playback, empty timeline guidance, source override during a preview render, file drops into the native media-pool viewport and timeline, multi-file ordering, internal pool MIME, copy-only drop actions, source preservation and reuse, undo/redo, and repeated player destruction. Actual double-click insertion and mouse-driven clip movement are exercised while source playback continues. Play/source confusion and refused file drops were observed failing before their fixes.

A user additionally reported g_object_unref/G_IS_OBJECT during clip work and possibly idle, without an exact reproducible source file or stack trace. That specific warning remains unconfirmed; passing critical-log tests is not a claim that every GStreamer/system-library issue is fixed. See FIXES_0.1.2.md for scope and remaining render-before-play limitation.
