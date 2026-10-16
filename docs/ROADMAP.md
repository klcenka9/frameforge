# Roadmap

Highest priority: integrate an asynchronous timeline playback engine (evaluate MLT 7 producers/tractor and Qt output), preserving the tested independent model and matching offline renders. Eliminate full preview renders before adding more workspaces.

Then:
- Expand safety tests: abrupt termination recovery, process timeouts, concurrent-instance prevention, disk-full and large-project tests; bounded disk/frame/thumbnail caches.
- Frame-exact viewer stepping, low-latency audio buffering, performance benchmarks and proxies; VFR/rotation/timebase and long-form A/V sync fixtures.
- Ripple/roll edits, linked audio controls, fades/transitions, bins, waveform analysis, titles, parameters/keyframes.
- Explicit color pipeline and scopes with OpenColorIO evaluation; a persistent validated compositing graph and real processing nodes; Fairlight-style gain/pan/automation and plugin hosting.
- Render queue, export presets and additional codec/container settings; runtime-tested hardware paths and CPU retry.
- Mint/Ubuntu native builds, Cinnamon/GNOME X11/Wayland, Intel/NVIDIA/hybrid hardware, multi-monitor/HiDPI and accessibility checks; distribution-specific Debian packages and then Flatpak.

Do not infer readiness from workspace names. Disabled planned workspaces have no implementation. This milestone does not claim professional production readiness.
