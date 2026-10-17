# Architecture decision — 2026-10-16

## Intent and scope
Build an independent native Linux editor, initially delivering import → edit → save/reopen → timeline preview → export with CPU processing. This is the first milestone of the supplied specification, not a completed professional editor. No proprietary code or artwork is used. Original branding: **Frameforge**.

## Options evaluated

| Option | Strengths | Costs / decision |
|---|---|---|
| Fork Kdenlive | Mature Qt/MLT editing, effects, proxies, audio and packaging | GPL-3.0 codebase and a large existing application architecture. Best recommendation if production maturity is needed now. Not forked for this small independent foundation. |
| Qt 6 + MLT 7 | Established producers, tractors, filters and consumers; used by mature editors | LGPL-2.1 framework, some plugins under other licenses. Adds profile, service and Qt preview integration work. Strong next engine candidate; not integrated yet. |
| Qt 6 + FFmpeg libraries / export process | Direct software decode for active clips, asynchronous playback, isolated final export | Used from 0.2.0; replaces the full-file preview of 0.1.x. |

Qt Widgets/Multimedia (6.4+) provide docks, accessibility, native dialogs and source playback. FFprobe probes on worker threads; FFmpeg renders via asynchronous QProcess. No shell interprets filenames. C++20 core owns rational project rate and integer-frame clips, validation and versioned JSON persistence. UI uses snapshot undo commands. Safe writes use QSaveFile. Recovery snapshots never overwrite source footage.

Two video and two audio tracks initially support nonoverlapping clips per track; higher video lanes composite over lower lanes; video clips include source audio, and audio lanes mix. Mute and solo affect rendering; locking prevents edits. Trims, split, move and delete retain source bounds. All media normalize to project resolution/rate for render. CPU encoders are checked at runtime. Hardware compiled-in encoder names are diagnostics only, never evidence of a working GPU.

From 0.2.0 TimelineDecoder reads active source streams through libavformat/libavcodec, converts video through libswscale and resamples stereo float audio to 48 kHz with libswresample. TimelinePlayer owns one worker and immutable project snapshots. Requests produce 40 ms blocks; generations cancel obsolete seeks/edits. Bounded PCM/frame queues and QAudioSink's processed-sample clock keep video and audio together. A monotonic clock supports silent playback when no audio output exists. Adjacent split clips retain decoder/resampler continuity. Preview resolution defaults to 960×540; export retains project resolution. Both paths share mute/solo rules, opaque video layer ordering and unity-gain audio summing with a ±0.95 peak clamp.

The GUI receives owned QImage/PCM buffers. It never waits for a full timeline render. Source browsing remains a separate Qt Multimedia player. Edits pause timeline playback and request a fresh frame; Space resumes without encoding or writing a preview. S / Ctrl+B split through one action shared by menu and toolbar, while typing S in editable controls remains text input.

Export writes a unique sibling temporary file, probes the result, and only then renames it to a new destination. Existing output and source files are refused. Empty projects, missing media, invalid graphs, absent tools and failed codecs produce errors.

## Deferred integrations
OpenTimelineIO (Apache-2.0) is a good interchange adapter after timeline semantics stabilize. OpenColorIO (BSD-3-Clause) needs explicit working/display transforms and floating-point image processing; no color-management claims now. OpenFX (BSD-style host/API, independently licensed plugins) needs a real image-processing host and plugin isolation; deferred. No third-party source is incorporated.

## Evidence and limits
Upstream license files and MLT README were fetched into docs/research. References: https://www.mltframework.org/ ; https://github.com/KDE/kdenlive ; https://ffmpeg.org/legal.html ; https://doc.qt.io/qt-6/licensing.html ; https://github.com/AcademySoftwareFoundation/OpenTimelineIO ; https://github.com/AcademySoftwareFoundation/OpenColorIO ; https://github.com/AcademySoftwareFoundation/openfx . Installed FFmpeg 7.1.5 enables GPL/libx264; Qt runtime 6.8.2. Exact dependency copyright files on the build host remain authoritative. API/ABI floor is Qt 6.4, not yet validated on Mint. No GPU hardware, real desktop, Mint, hybrid laptop or long-form performance certification is available here.
