# Frameforge foundation implementation plan

Goal: a runnable C++20/Qt 6 editor with tested frame-based edits and real CPU rendering.
Spec: supplied user brief; milestone design in docs/ARCHITECTURE.md.
Execution: implement inline, as requested by the user's autonomous incremental workflow.

- [x] Core: Project, Media, Track, Clip; source bounds and overlap validation; split/move/trim/delete; versioned safe persistence. Qt Test exercises roundtrip, malformed files, missing sources and edit invariants before UI integration.
- [x] Media: asynchronous FFprobe and thumbnail jobs; FFmpeg composition graph, progress, cancellation and safe finalization. Generated color/tone integration fixtures verify duration, audio/video output and cancellation.
- [x] Desktop: docks for media, preview, inspector and timeline; shared project; keyboard editing, snapshot undo, recovery; source and rendered-timeline playback. Qt offscreen smoke test launches and exercises the real UI.
- [x] Delivery: CMake/CTest, desktop entry and CPack Debian recipe; build/run/recovery documentation and explicit unfinished-work roadmap.

Review focus: malformed project must not replace current state; export must not overwrite sources; media failures must preserve edits; muted/solo tracks must affect render consistently; cancellation must remove temporary output. Tests cover these conditions at the owning layer.
