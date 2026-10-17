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

## Direct playback verification (0.2.0)

Debian 13 / Qt 6.8.2 / FFmpeg 7.1.5: Release build and 5/5 CTest suites passed in 13.69 seconds during implementation. Tests now cover software-decoded trimmed frames, video layer order, gaps, audio summing and silence, sample-level split continuity (including 30000/1001 fps), VFR and nonzero source timestamps, missing/corrupt inputs, cancellation, rapid seeks, generation changes, teardown, and direct playback on a ten-hour timeline without a preview file. Preview image colors and decoded PCM levels are compared against explicit exports.

Desktop tests now require Play without RenderJob, pause/resume, source isolation, seeking, actual S/Ctrl+B events, no-selection split fallback, undo/redo, boundary/locked-track guidance without modal interruption and typing S in text inputs. Existing drag/drop, import/relink/save/recovery tests remain. Default headless suites intentionally skip the optional audio-device test; run `bash scripts/test-audio.sh build-release` to start a private PulseAudio null sink and verify the device-clock path. That test passed locally (3 Qt Test results including hooks, 7.672 seconds). Physical speaker output remains unverified.

The earlier user stack trace does establish a critical during GStreamer message-structure disposal. Its exact root cause remains unresolved. Direct timeline playback uses software libav decoding; source browsing still uses Qt Multimedia and may still encounter the reported platform warning.

Final local 0.2.0 regression suite after review fixes: **5/5 suites passed in 30.57 seconds** (concurrent installer build increased run time). Independent review identified off-grid trim/VFR sampling parity, default-versus-first stream selection, exhausted video-layer composition, and stalled audio clocks. New tests reproduced all four before the fixes. Export now samples with hold-last onto the project frame grid, live/export explicitly use first video/audio streams, expired video frames reveal lower layers, and a suspended-device test verifies monotonic silent fallback after the three-second watchdog. Audio teardown discards buffers with reset instead of draining a suspended device.

The extended private audio test passed **4/4 Qt Test results in 13.713 seconds**, including normal device-clock transport and deliberately suspending the sink. A clean source-install build with `--no-deps --source` completed in a fresh temporary directory. The installed binary dynamically links libav and has no build-workspace RUNPATH. The Debian package declares the actual libav ABI dependencies.

Initial Ubuntu 24.04 / Qt 6.4.2 / FFmpeg 6.1 CI passed all five suites, virtual audio and native X11: https://github.com/klcenka9/frameforge/actions/runs/39016160916 . Final review-fix CI is recorded with the release.

Final reviewed-source Ubuntu24.04 / Qt6.4.2 / FFmpeg6.1 verification passed configure, build, **5/5 CTest suites**, private audio (including suspended-device fallback) and native X11: https://github.com/klcenka9/frameforge/actions/runs/39016590117 . Final Debian X11 desktop run passed 8/8 Qt Test results in 5.223 seconds.

Published-release round trip: GitHub release39016926681 successfully uploaded ZIP, DEB, source and checksums. Downloaded copies matched local/published SHA256SUMS; ZIP integrity check passed for61 files. The actual public v0.2.0 installer also completed source download, fresh build and install with existing SDK dependencies (`--no-deps`, temporary per-user prefix). The sudo/apt branch is not run in this managed workspace; Ubuntu CI validates dependency package availability.
