Frameforge 0.1.2 — timeline playback and drag/drop fixes

Close the old application, then update/install from source:

```sh
curl -fsSL https://raw.githubusercontent.com/klcenka9/frameforge/v0.1.2/scripts/install.sh | bash
```

Launch: `~/.local/bin/frameforge`. Targets: Mint 22+, Ubuntu 24.04+, Debian 12+ with Qt 6.4+.

- Play timeline / Space now plays edited content and advances/scrolls the playhead, automatically building a background preview after edits. Source playback has a separate button. Empty timelines show clear guidance.
- Drop local files into the media pool/window or directly onto a timeline track; drag imported media onto tracks. Ordered batch placement, undo/redo, source reuse and copy-only actions preserve original files.
- Avoid duplicate source reloads and detach player outputs explicitly before destruction. Desktop tests treat GLib criticals as fatal.

Verification: Debian 13 and Ubuntu 24.04/GStreamer each passed 4/4 CTest suites; X11 desktop tests passed. Tests include actual double-click insertion and mouse-driven clip movement while playback continues. The supplied g_object_unref/G_IS_OBJECT warning was not reproduced, so a conclusive fix for that specific library warning is not claimed.

Assets include the complete source/binary ZIP, Debian 13 x86_64 / Qt 6.8.2 package, tagged source and SHA256 checksums. The Debian package is not a universal Mint binary; use the source installer for other target distributions. Timeline previews still require background rendering; advanced workspaces remain planned.
