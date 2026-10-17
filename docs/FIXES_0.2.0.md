# Frameforge 0.2.0 — direct timeline playback

Close the old editor before updating:

```sh
curl -fsSL https://raw.githubusercontent.com/klcenka9/frameforge/v0.2.0/scripts/install.sh | bash
```

Launch `~/.local/bin/frameforge`. Existing schema-1 project files remain supported.

## Editing and transport

- **S / Ctrl+B** splits at the playhead. One shared action shows both shortcuts in the menu/toolbar. Without a selection, it chooses the topmost editable clip under the playhead. Typing S in an editable field remains text input. Boundaries/locked tracks show status guidance rather than opening a modal warning.
- **Space / Play timeline** reads the source clips directly. Splitting, editing, seeking, frame stepping, pause and resume never encode an MP4 preview or call RenderJob.
- Edits pause at the current playhead and request the updated frame. Space resumes without rebuilding the timeline into a file.
- Trimmed sources, video layers, black gaps, video EOF reveal, mixed audio, track mute/solo and split continuity are supported. Preview defaults to 960×540; final export uses project resolution.
- Video samples follow source timestamps on the project frame grid. Live playback and export use the first video/audio streams even if another stream is marked default.
- Decode work runs off the GUI thread. Obsolete seeks/edits are cancelled by generation and cannot present stale frames. Queues contain short blocks, not a full timeline.
- Audio consumption clocks playback. With no device, playback uses a silent monotonic clock. If an output clock stalls for three seconds, queued audio is discarded and silent playback continues without jumping backward. Pause/Play retries audio. Loading is shown in the viewer.

## Explicit final export

Only Deliver's Render/Export control starts an export. Exports still use an immutable snapshot, software FFmpeg processing, output validation and overwrite protection. Live/export audio now share unity-gain summing with a hard peak clamp at ±0.95; this replaces the previous export-only lookahead limiter and can change the sound of loud overlaps.

## Validation and limits

See [TESTING.md](TESTING.md) for local/Ubuntu CI evidence, generated fixture coverage and the independently reviewed fixes. A current native desktop capture is [desktop-x11.png](desktop-x11.png).

This remains an experimental editor. Proxy workflows, effects, color management and advanced audio are not implemented. 4K/8K real-time performance, physical speakers, Mint/Cinnamon and hybrid GPU hardware are not certified. Explicit export retains the existing 64-clip graph limit. Metadata rotation remains unverified. The previously reported GLib/GStreamer source-player warning remains unresolved; direct timeline playback is not evidence that source browsing is fixed.

The source installer targets Mint22+/Ubuntu24.04+/Debian12+ with Qt6.4+ and FFmpeg5.1+ development libraries. The supplied binary/DEB is specifically for **Debian13 x86_64 / Qt6.8.2 / FFmpeg7.1**, not a universal Mint binary. The complete ZIP includes source, this binary, the DEB and a source archive; all require distribution runtime dependencies.
