# Direct timeline playback implementation plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan inline. Steps use checkbox syntax for tracking.

**Goal:** Play edited timelines immediately without encoding a preview; expose Split on S and Ctrl+B.

**Architecture:** An owned software decoder per active clip/stream produces bounded video/audio blocks. A worker samples immutable timeline snapshots and returns owned buffers. TimelinePlayer presents frames on the GUI thread, clocks playback from QAudioSink where available, and cancels obsolete generations on seek/edit.

**Tech Stack:** C++20, Qt 6.4+, libavformat/libavcodec/libavutil/libswscale/libswresample, existing FFmpeg CLI for export.

**Spec:** ../../live-playback-design.md (approved in conversation).

## Global constraints

- No RenderJob or preview file on Play, Split, seek, pause or edits.
- Preserve project schema, export validation, mute/solo/layer ordering and undo/redo.
- Owned buffers, generation cancellation, software decoding, bounded queues, 48 kHz stereo PCM.
- Export remains explicit. GLib source-player warning is not a verified fix.

## Review focus

- Variable source timestamps and nonzero stream starts: choose by PTS, not frame index.
- Rapid edit/seek: stale frames and samples must never reach the viewer/audio device.
- Missing audio device/underrun: maintain an explicit monotonic fallback clock.
- Decoder shutdown/corrupt inputs: cancel and release resources without GUI hangs.
- Shortcut focus: entering S in a text field must not split clips.

### Task 1: Timeline sampler and software decoding

**Files:** src/media/timeline_decoder.{h,cpp}, tests/playback_tests.cpp, CMakeLists.txt.
**Interface:** TimelineDecoder(Project, QSize, cancellation predicate); VideoFrame video(qint64 frame); QByteArray audio(qint64 startSample, int samples). Buffers are owned and output audio is stereo float at 48000 Hz. Clip/stream decoders retain sequential state but release inactive inputs.

- [x] Add fixture-driven tests for trimmed colors, layer order, gaps, split continuity, mute/solo, mixed audio, missing/corrupt inputs, cancellation and long project seek.
- [x] Run playback_tests and observe failure before implementation.
- [x] Implement libav decoding with timestamp-based seeking, pixel conversion and resampling; share visibility ordering/gain rules with export where appropriate.
- [x] Run sampler tests including reference FFmpeg exports; inspect results before committing.

### Task 2: Asynchronous playback transport

**Files:** src/media/timeline_player.{h,cpp}, tests/playback_tests.cpp, CMakeLists.txt.
**Interface:** TimelinePlayer::setProject(Project), play(), pause(), seek(qint64), setPreviewSize(QSize), frame()/isPlaying(); signals frameReady(QImage), positionChanged(qint64), error(QString), ended().

- [x] Test first-frame latency on a long timeline, pause/resume, seek latest-wins, end, no audio-device fallback, queue bounds and teardown while decoding.
- [x] Run tests red; implement worker requests with one bounded result block, cancellation generations and GUI presentation/audio clock.
- [x] Run tests green; verify no subprocess/render output is produced.

### Task 3: Editor integration and split shortcuts

**Files:** src/ui/editor.{h,cpp}, tests/desktop_tests.cpp.

- [x] Update desktop tests to require Play without RenderJob and verify seek/pause, source isolation, edit refresh, actual S/Ctrl+B events, no-selection fallback and text input focus.
- [x] Run red against existing editor.
- [x] Replace preview-render transport with TimelinePlayer and image viewer; retain separate source QMediaPlayer view. Remove preview render controls/cache/state. Keep RenderJob only for explicit export.
- [x] Share one Split QAction with toolbar; scope S away from editable fields and avoid modal boundary/locked errors.
- [x] Run desktop + HiDPI and native X11 tests green.

### Task 4: Dependencies, release and review

**Files:** scripts/install.sh, .github/workflows/build.yml, CMakeLists.txt, README.md, docs, release packaging.

- [x] Add libav dev dependencies and pkg-config; update runtime package dependencies and direct-playback documentation.
- [x] Run all CTest suites, source installer smoke check, clean Release build and package validation.
- [x] Fresh review of whole diff; fix important findings with regression tests, rerun suite.
- [ ] Publish updated source and built artifacts to the user-authorized repository only after validation; record platform limits accurately.

## Execution ledger

- Approved design: user replied “Jo”. Execute inline in current checkout; no unrelated working changes exist.
- Plan review: interfaces cover timestamp decoding, transport and GUI; extra implementation approval is unnecessary under the standing user instruction to perform the work.

- Tasks 1–3: implementation and regression tests complete. Missing decoder/player interfaces and editor integration tests were observed red; decoder/transport and desktop tests then passed. Additional failing tests caught fractional-frame seek rounding, timestamp-quantized audio jitter, split phase continuity and export channel layout; these passed after corrections.
- Full-suite checkpoint: `ctest --test-dir /workspace/build-live --output-on-failure` → 5/5 passed, 13.69 seconds. Optional device-clock case → 3/3 Qt Test results passed against PulseAudio null sink.
- Shared audio rule: use unity-gain summing and hard peak clamp ±0.95 in both paths instead of keeping the old export-only lookahead limiter. This intentionally changes gain/peak behavior for loud overlaps; prevents live/export gain mismatch.

- Final review: fresh-context reviewer found no Critical issues, four Important ones (off-grid sampling parity, multistream policy, video EOF layering, audio-clock stalls). All four reproduced red in dedicated tests and passed after fixes; full suite 5/5, 30.57 seconds. The optional suspended-device case failed before watchdog/reset changes and passed afterwards; private audio tests 4/4, 13.713 seconds.
- Review minors: design status and task checkboxes were already updated at the checkpoint; no deferred minor findings. Source-player GLib warning remains unresolved, as documented.
- Additional validation: clean source-installer build/install completed; CPack DEB built and inspected for actual libav dependencies, 0755 directories and no workspace RUNPATH. Initial Ubuntu24/Qt6.4.2 CI 39016160916 passed all suites + virtual audio + X11.

- Final reviewed-source Ubuntu24.04 CI 39016590117: success for build, full CTest, private audio suspension/fallback and native X11. Final local X11: 8/8, 5.223 seconds. Product source is unchanged after this validated checkpoint; remaining changes are documentation and release assets.
