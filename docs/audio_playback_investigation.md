---
name: Audio Playback Investigation
description: Comprehensive record of all attempts to add audio playback to ShaderPlayer, including failed architectures, bugs found, and the unsolved choppy playback problem. Reference for future sessions.
type: project
---

# Audio Playback Investigation (2026-03-10 to 2026-03-20)

## Goal

Add audio playback through speakers when playing a video file in ShaderPlayer, matching the quality of ffplay/MPC-HC. Audio recording (muxing as AAC into output files) already works perfectly.

## Current State (as of revert to fd4125a)

- **AudioPlayer class was NEVER committed** — `src/AudioPlayer.h` and `src/AudioPlayer.cpp` exist only in session work, not in the repo
- **VideoEncoder audio recording works** — AAC mono stream muxing is confirmed working
- **VideoDecoder has basic audio decode** — `OpenAudioStream()`, `DecodeAudioPacket()`, `DrainAudioSamples()` exist in the committed codebase but are single-threaded (called inline during `DecodeNextFrame`)
- **No AudioPlayer, no WASAPI, no playback** in the committed code
- The committed `ProcessFrame()` drains audio samples only for the analyzer, not for playback
- No read thread, no packet queues in the committed VideoDecoder

## What Works (confirmed across sessions)

1. **Audio recording**: VideoEncoder accepts mono float samples via `SubmitAudioSamples()`, encodes AAC, muxes correctly
2. **Audio decode**: VideoDecoder's SWR pipeline (any source format → mono FLTP) produces correct samples
3. **WASAPI initialisation**: Event-driven shared mode, 20ms buffer, correct format negotiation
4. **Ring buffer design**: SPSC lock-free, 131072 stereo frames (~2.73s at 48kHz), power-of-2 masking

## Failed Architecture 1: Main-Thread Audio Drain

### Design
- `DecodeNextFrame()` calls `av_read_frame()` in a loop; audio packets decoded inline via `DecodeAudioPacket()` and appended to `m_audioPending`
- Main thread's `ProcessFrame()` calls `DrainAudioSamples()` then `AudioPlayer::Submit()`

### Why It Failed
- `av_read_frame()` is called only when `DecodeNextFrame()` is called (once per video frame, ~30-60 fps)
- Audio packets interleaved with video packets in the container; at 30fps video, audio gets ~30 decode opportunities/sec
- For 48kHz audio, need ~47 audio packets/sec decoded — insufficient throughput
- Audio buffer drains faster than it fills → choppy playback

### Specific Bug: `ReadAudioAhead if (got <= 0) break;`
A helper `ReadAudioAhead()` was added to read ahead for audio packets, but the first packet read is almost always video. `DrainAudioSamples` returns 0 for video packets, triggering the `break` immediately. Zero audio submitted per tick.

## Failed Architecture 2: Dedicated ReadThread (ffplay Model)

### Design
- Dedicated `ReadThread()` calls `av_read_frame()` continuously in its own thread
- Video packets → `m_videoPktQueue` (bounded queue, 32 packets)
- Audio packets → decoded immediately via `DecodeAudioPacket()`, appended to `m_audioPending`
- Main thread reads video from queue, drains audio from `m_audioPending`

### Why It Partially Failed
The ReadThread successfully decoupled audio decode from video frame rate, but the main thread was still in the audio playback critical path:

1. **m_audioPending unbounded growth**: ReadThread decoded entire file's audio into m_audioPending in seconds. Drain loop emptied all into Submit(), which dropped everything beyond ring buffer capacity.
   - **Fix**: Added 5-second cap on m_audioPending in ReadThread + ring target in drain loop

2. **Audio packet discard in throttle**: When m_audioPending was full, code did `av_packet_unref(m_packet); continue;` — permanently discarding audio data.
   - **Fix**: Changed to wait loop that holds the packet until m_audioPending has space

3. **Video queue throttle starving audio**: ReadThread waited on video queue BEFORE calling `av_read_frame()`, blocking both audio and video reads. At 30fps video, audio only got ~30 decode opportunities/sec.
   - **Fix**: Moved `av_read_frame()` to top of loop (unconditional), applied per-stream throttles AFTER reading

4. **False EOF detection**: When video queue empties (ReadThread blocked on audio throttle), `DecodeNextFrame` times out and returns false. ProcessFrame treated this as EOF: `m_audioPlayer.Flush()` + `m_decoder.SeekToTime(0.0)`, destroying all buffered audio.
   - **Fix**: Added `IsAtEnd()` method using `m_readThreadEOF` atomic; only trigger EOF handling when truly at end of file

5. **Main thread still in audio critical path (ROOT CAUSE)**: Even with all fixes, `AudioPlayer::Submit()` was called from the main thread's `ProcessFrame()`. GPU work and video decode stall the main thread. Higher-resolution video = longer stalls = worse audio underruns. User reported: "starts fine but gets choppy after 200 frames", "worse on higher resolution video".

## Failed Architecture 3: ReadThread Calls Submit() Directly

### Design (implemented in this session, reverted)
- `VideoDecoder` holds `std::atomic<AudioPlayer*> m_audioPlayer`
- `DecodeAudioPacket()` calls `m_audioPlayer->Submit()` directly on the ReadThread
- Also appends to `m_audioPending` for analyzer/encoder (main thread consumers)
- Main thread drain loop simplified: only feeds analyzer and encoder, no Submit
- `AudioPlayer::m_sourceRate` made `std::atomic<int>` for thread safety
- `m_swrMutex` added to protect SWR context from concurrent Submit/Flush

### Why It Failed
- **New bug introduced**: Audio started playing immediately when video loaded (before user pressed Play). Root cause not investigated before revert.
- Choppy playback issue was still present (not confirmed fixed since the autoplay bug made testing difficult)

### Thread Safety Issues Identified
- `Submit()` uses `m_swr` (SWR resampler context) which is not thread-safe
- `Flush()` on main thread resets `m_sourceRate` to 0, forcing SWR reinit on next `Submit()` call from ReadThread
- Solution attempted: `m_swrMutex` in AudioPlayer guarding both `Submit()` and `Flush()`
- Concern: mutex in audio path could introduce its own latency

## AudioPlayer Implementation Details (for reference when rebuilding)

### WASAPI Setup
```
- CoCreateInstance(MMDeviceEnumerator) → GetDefaultAudioEndpoint(eRender, eConsole)
- GetMixFormat() → accept whatever shared-mode format is
- Initialize(SHARED, EVENTCALLBACK, 20ms buffer)
- Event-driven AudioThread at THREAD_PRIORITY_TIME_CRITICAL
```

### Ring Buffer
```
- SPSC: producer = Submit thread, consumer = AudioThread
- kRingCap = 131072 (1<<17) stereo frames
- Power-of-2 masking (kRingMask = kRingCap - 1)
- Raw counters (m_wPos, m_rPos) never wrap in a reasonable session
- Available to read = wPos - rPos
- Available to write = kRingCap - (wPos - rPos)
```

### SWR Resampler
```
- Input: mono FLTP at source sample rate (from VideoDecoder)
- Output: interleaved stereo FLT at WASAPI rate
- Reinit when source sample rate changes (or after Flush sets m_sourceRate=0)
```

### Key MSVC Gotcha
`AV_CHANNEL_LAYOUT_MONO` is a compound literal — MSVC C++ mode rejects `&AV_CHANNEL_LAYOUT_MONO`. Must assign to a local variable first:
```cpp
AVChannelLayout monoLayout = AV_CHANNEL_LAYOUT_MONO;
av_channel_layout_copy(&ctx->ch_layout, &monoLayout);
```

### INITGUID Requirement
`AudioPlayer.cpp` must `#define INITGUID` before the first `windows.h` include, or WASAPI GUIDs are only declared (not defined) and linking fails.

## Key Observations

1. **Recording audio is trivially easy** — the encoder thread is already decoupled from the main thread. Samples are queued via mutex, encoded asynchronously. This path works perfectly.

2. **Playback audio is hard because of real-time constraints** — WASAPI needs continuous sample delivery at precise timing. Any gap > ~20ms causes audible glitches.

3. **The main thread is unsuitable for audio delivery** — it runs at video frame rate (30-60fps = 16-33ms per tick), is blocked by GPU present calls, video decode, ImGui rendering, and D3D11 state setup. Any of these can cause >20ms stalls.

4. **The ReadThread is a better candidate** — it runs continuously and independently. But it has its own throttle waits (video queue full, audio pending full) that could introduce gaps.

5. **ffplay's approach**: Separate audio and video clocks. Audio drives the master clock. Dedicated audio callback thread. Video syncs to audio, not the other way around. ShaderPlayer's architecture is the opposite: video drives everything, audio is bolted on.

## Approaches NOT Yet Tried

1. **Separate audio decode thread** (not just separate demux thread): A thread dedicated solely to audio that owns its own SWR context, decodes audio packets from a queue, and calls Submit() with no contention from video at all.

2. **WASAPI render callback feeding directly from decoded audio queue**: Instead of the ring buffer + separate AudioThread approach, use IAudioClient::SetEventHandle with a callback that pulls directly from a decoded sample queue. This eliminates one copy.

3. **Audio clock as master** (ffplay model): Let audio playback drive timing. Video syncs to audio position rather than wall clock. This is the standard approach in media players but requires significant architectural changes to ShaderPlayer.

4. **Pre-decode entire audio track**: For files (not live capture), decode the entire audio stream upfront into a large buffer. Submit from a simple timer thread. Avoids all demux interleaving issues. Memory cost: ~5.5 MB/minute for mono float at 48kHz.

5. **Use an existing audio library**: SDL_audio, PortAudio, or miniaudio provide simpler abstractions over WASAPI with built-in buffering and threading. Would replace the hand-rolled WASAPI code.

6. **Double-buffer approach**: Two large buffers (e.g. 1 second each). ReadThread fills one while AudioThread drains the other. Swap when the drain buffer empties. Eliminates fine-grained synchronisation.

## Files That Need Changes (from committed state at fd4125a)

- `src/AudioPlayer.h` — DOES NOT EXIST, must be created
- `src/AudioPlayer.cpp` — DOES NOT EXIST, must be created
- `src/Application.h` — needs `#include "AudioPlayer.h"`, `AudioPlayer m_audioPlayer` member, volume/mute methods
- `src/Application.cpp` — needs AudioPlayer lifecycle, Submit/Flush calls, volume/mute UI wiring
- `src/Common.h` — needs `audioVolume` (float) and `muteAudio` (bool) in AppConfig
- `src/ConfigManager.cpp` — needs audioVolume/muteAudio serialisation
- `src/UIManager.cpp` — needs mute button and volume slider in transport bar
- `src/VideoDecoder.h` — may need ReadThread additions depending on architecture chosen
- `src/VideoDecoder.cpp` — may need ReadThread, packet queues, DecodeAudioPacket changes
- `CMakeLists.txt` — needs AudioPlayer.cpp added to sources

## VideoEncoder Audio (Working, Do Not Change)

The `VideoEncoder` audio recording path works and should not be modified:
- `SubmitAudioSamples(mono, count, sampleRate)` — thread-safe accumulator
- `EncodeAudioIfAvailable()` — drains accumulator into AAC frames
- `FlushAudio()` — called at stop
- `InitEncoder` creates audio stream with `AV_CODEC_ID_AAC`, `AV_SAMPLE_FMT_FLTP`, mono, 128kbps
- Audio stream created BEFORE `avformat_write_header`
- PTS: `m_audioFrameIndex * 1000LL` (time_base = {1, sampleRate*1000})
