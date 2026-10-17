Frameforge 0.2.0 — direct timeline playback

Close the old application, then update/install:

```sh
curl -fsSL https://raw.githubusercontent.com/klcenka9/frameforge/v0.2.0/scripts/install.sh | bash
```

Launch: `~/.local/bin/frameforge`. Source-install targets: Mint22+, Ubuntu24.04+, Debian12+, Qt6.4+ and FFmpeg5.1+.

- Split: **S / Ctrl+B**, visible in menu and toolbar. Without a selection, split the topmost editable clip under the playhead. Typing S stays text input.
- **Space / Play timeline** plays directly from source clips. Split, edits, seeking and pause/resume never encode a preview file. Export runs only from Deliver's Render/Export control.
- Trimmed frames, layers, gaps, stream EOF reveal and audio mixing follow timeline timestamps. Adjacent splits retain audio continuity, including fractional frame rates. Live/export use first video/audio streams and consistent hold-last sampling.
- Bounded worker queues, generation cancellation and explicit loading feedback keep seeks/edits independent from final export. With an absent/stalled device, playback can continue silently on a monotonic clock; Pause/Play retries audio.

Verification: Debian13 and Ubuntu24.04 each passed **5/5 CTest suites**. Private PulseAudio tests passed normal transport and deliberately suspended-device fallback (4/4 Qt Test results). Native X11 passed on both environments. Clean source installer build/install and Debian package dependency/RUNPATH checks passed. Final Ubuntu evidence: https://github.com/klcenka9/frameforge/actions/runs/39016590117 . Review fixes were covered by observed failing-then-passing regressions.

ZIP includes complete source, built executable, DEB and source archive. The binary/DEB is specifically **Debian13 x86_64 / Qt6.8.2 / FFmpeg7.1**, not a universal Mint binary; use the source installer elsewhere. Preview defaults to960x540; export retains project resolution. Live/export audio now share a hard ±0.95 peak clamp, replacing the previous export-only limiter.

Experimental limits: final export retains the existing64-clip graph limit; proxies, effects and color management are not implemented; physical speakers, Mint/hybrid hardware and4K/8K real-time performance remain unverified. The reported GLib/GStreamer source-browsing warning is still unresolved; this release does not claim a proven fix for it.
