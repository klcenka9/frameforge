# Direct timeline playback

Status: approved and implemented for 0.2.0; regression tests and independent review completed. Replaces the full-file preview used by 0.1.2.

## Required behavior

Editing, splitting, seeking, and Play must never invoke RenderJob or encode a
preview file. Render is an explicit export action. Playback must reflect source
in-points, clip durations, gaps, video layer order, and audible tracks. Preserve
existing project files and undo/redo behavior.

Split uses Ctrl+B and S, with both shown in the UI. A single shared QAction
backs the menu and toolbar. S must not split while typing in an editable field.
If no clip is selected, split the topmost editable clip under the playhead;
otherwise split the selected clip. Report boundary/locked-track cases without
opening a modal warning during normal editing.

## Approaches considered

1. Switch QMediaPlayer between source files. Small change, but cannot correctly
   mix multiple audio tracks or compose simultaneous video tracks. Not sufficient.
2. Stream an FFmpeg filter graph into the viewer. Reuses export filters, but a
   graph covering the entire project opens many inputs, makes seeking expensive,
   and still needs explicit bounded buffering and audio synchronization.
3. Decode active clips directly with libavformat/libavcodec, resample audio with
   libswresample, and convert video with libswscale. Recommended: only active and
   immediately upcoming clips need decoding. No preview encoder or preview file.

## Playback component

Introduce TimelinePlayer with play, pause, seek(frame), setProject(snapshot),
positionChanged, frameReady, ended, and error. Editor delegates timeline
transport to this component; QMediaPlayer remains isolated for source browsing.
RenderJob is used only by the explicit export action.

Decode on worker threads. GUI operations never wait for a decoder. Every seek
or project change increments a generation; discard results from older
generations. Cancel workers and join them before releasing their resources.
Pass immutable project snapshots and owned frame/sample buffers across threads.

Use software decoding initially so playback does not depend on the machine's
GStreamer/NVIDIA integration. Seek to the preceding keyframe and discard samples
before the requested source timestamp. Honor stream time bases and start times;
do not assume frame index equals decoder output index for variable-rate inputs.

At each timeline timestamp, choose visible clips by the same track ordering and
mute/solo rules as export. Scale/pad to the monitor's preview resolution and
compose video over black. Decode only needed intervals. Mix audible clips into
48 kHz stereo PCM with unity gain and a hard peak clamp at ±0.95, shared with export. Gaps produce black and
silence. Trim output at clip boundaries; adjacent split clips retain continuous
source timestamps.

QAudioSink consumption is the playback clock when an audio device is available.
Use a monotonic clock when no audio device exists. Pause freezes timeline time;
resume and seeking reset the clock origin. Bound decoded video queues to a small
number of frames and audio queues to at most 250 ms. Drop late display frames
rather than allowing unbounded memory or audio drift. Surface underruns/loading. If the device clock stops for three seconds, discard
queued audio and continue with a monotonic silent clock at the last observed
sample; never jump backward. Pause/Play retries the device. Ordinary decoding
underruns show loading while the audio clock waits for samples. Preload the next clip within those limits.

Editing while playing pauses at the current timeline frame, invalidates old
decoded results, and shows the new frame. Space resumes immediately without a
render. Scrubbing uses the same decoder path with latest-request-wins behavior.

## Integration

Remove Render timeline preview and all automatic preview rendering from Play.
Keep the explicit final export controls and existing output validation. Share
timeline visibility/audio rules with export so the two paths cannot diverge
silently. Add libav development packages to CMake, installer, packaging and CI;
the existing ffmpeg CLI remains required for export and probing.

Expose Play/Pause (Space) and Split (S / Ctrl+B) in toolbar labels/tooltips and
the help text. Avoid S conflicts in text inputs, spin boxes and shortcut menus.

## Verification required before release

- Split via actual S and Ctrl+B key events, undo/redo, selection fallback,
  locked tracks, clip boundaries, and text-entry focus.
- Start, pause/resume and seek across trimmed clips, gaps and adjacent splits;
  verify displayed source frames and timeline positions.
- Compare overlapping video tracks and mixed audio against export fixtures.
- Verify mute/solo, audio-only/video-only clips, variable frame rates, absent
  audio devices, missing/corrupt inputs and decoder errors.
- Edit and seek rapidly while decoding; prove old-generation frames cannot
  overwrite current frames and process teardown is safe.
- Use a long timeline fixture to prove the first frame arrives before the full
  project could be processed; assert no RenderJob or preview output is created.
- Check bounded memory/queues and audio/video synchronization over repeated
  clip boundaries on Ubuntu 24.04 and the existing Debian environment.

The reported GLib warning remains a separate unresolved issue in source
QMediaPlayer playback. Replacing timeline playback must not be described as a
verified fix for that warning without a corresponding reproduction and test.
