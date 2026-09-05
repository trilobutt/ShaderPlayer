#include "Application.h"
#include "FrameProfiler.h"
#include "ui/MainWindow.h"
#include "ui/ViewportWidget.h"
#include <shellapi.h>
#include <cstdint>
#include <fstream>
#include <future>
#include <vector>

#include <QCoreApplication>
#include <QFileDialog>
#include <QString>

namespace SP {

namespace {

// Every notice Application raises goes to the window's toast stack. Null-tolerant
// because OpenVideo and friends are reachable from InitializeQt, and because the
// crash handler's Shutdown path runs with the window already gone.
void Toast(MainWindow* window, const std::string& message) {
    if (window) window->ShowToast(QString::fromStdString(message));
}

}  // namespace

Application::Application() = default;

Application::~Application() {
    Shutdown();
}

// Precondition: a QApplication must already exist before this is called — a QWidget
// (MainWindow, and everything MainWindow builds) cannot be constructed without one.
// WinMain constructs it first and calls this.
bool Application::InitializeQt() {
    // Load configuration
    m_configManager.Load(ConfigManager::GetDefaultConfigPath());

    // ---- CPU-only prep, run alongside D3D11CreateDevice --------------------------------
    // D3D11CreateDevice spends the better part of half a second inside the display driver
    // with this thread doing nothing at all. Three pieces of startup work need no D3D
    // device to produce their result — the noise texture's pixels, the shader sources and
    // their ISF metadata, and the audio output device — so they are started here and
    // collected further down, each immediately before the first thing that needs it.
    //
    // Nothing launched here touches m_renderer, m_shaderManager or any Qt object. The
    // config is read but not written until every job has been collected.
    const AppConfig& startupConfig = m_configManager.GetConfig();
    const int noiseSize = D3D11Renderer::ClampNoiseSize(startupConfig.noise.textureSize);

    std::future<std::vector<uint8_t>> noiseJob = std::async(
        std::launch::async,
        [scale = startupConfig.noise.scale, noiseSize] {
            return D3D11Renderer::GenerateNoisePixels(scale, noiseSize);
        });

    std::future<std::vector<ShaderPreset>> presetJob = std::async(
        std::launch::async,
        [&presets = startupConfig.shaderPresets] {
            std::vector<ShaderPreset> loaded;
            loaded.reserve(presets.size());
            for (const auto& configPreset : presets) {
                if (configPreset.filepath.empty()) continue;

                ShaderPreset preset;
                if (!ShaderManager::LoadShaderMetadataFromFile(configPreset.filepath, preset))
                    continue;

                preset.shortcutKey       = configPreset.shortcutKey;
                preset.shortcutModifiers = configPreset.shortcutModifiers;

                // Restore saved param values and keyframe timelines by name
                for (auto& param : preset.params) {
                    auto it = configPreset.savedParamValues.find(param.name);
                    if (it != configPreset.savedParamValues.end()) {
                        const auto& vals = it->second;
                        for (int i = 0; i < 4 && i < static_cast<int>(vals.size()); ++i)
                            param.values[i] = vals[i];
                    }
                    auto kit = configPreset.savedKeyframes.find(param.name);
                    if (kit != configPreset.savedKeyframes.end()) {
                        param.timeline = kit->second;
                    }
                }
                loaded.push_back(std::move(preset));
            }
            return loaded;
        });

    // miniaudio opens the WASAPI device itself and initialises COM on whatever thread
    // calls it, so this is free to run here. Collected before OpenVideo below, which is
    // the first thing on this thread to touch the player.
    std::future<void> audioJob = std::async(
        std::launch::async,
        [this, volume = startupConfig.audioVolume, mute = startupConfig.muteAudio] {
            // Non-fatal — the app continues without audio on headless systems.
            if (m_audioPlayer.Initialize()) {
                m_audioPlayer.SetVolume(volume);
                m_audioPlayer.SetMute(mute);
            }
        });

    // Initialize D3D11 — device only; every swap chain in the process belongs to the
    // window that presents it (ViewportWidget, VideoOutputWindow).
    //
    // No window is needed here, and that is what lets MainWindow be constructed *after*
    // the subsystems its panels read. Building the window first is an access violation:
    // LibraryPanel's constructor calls Refresh(), which dereferences GetShaderManager(),
    // and that unique_ptr is not filled until further down.
    //
    // The size is provisional. ViewportWidget::resizeEvent calls D3D11Renderer::Resize
    // with the real viewport size as soon as the window lays out, and the display texture
    // is sized per frame from the content resolution regardless.
    if (!m_renderer.Initialize(m_configManager.GetConfig().generativeWidth,
                               m_configManager.GetConfig().generativeHeight)) {
        MessageBoxA(nullptr, "Failed to initialize D3D11", "Error", MB_OK | MB_ICONERROR);
        return false;
    }

    // Upload the noise texture the worker built while the device was coming up (bound
    // globally as t1/s1 for all shaders).
    {
        const auto& cfg = m_configManager.GetConfig();
        // A bad_alloc from the noise buffer is a texture the shaders can do without, not a
        // reason to terminate the process before the window has been built.
        try {
            m_renderer.UploadNoisePixels(noiseJob.get(), noiseSize);
        } catch (const std::exception&) {
        }
        m_renderer.SetGenerativeResolution(cfg.generativeWidth, cfg.generativeHeight);
    }

    // Initialise Spout sender (non-fatal — Spout may not be installed on this machine)
    if (m_spoutOutput.Initialize(m_renderer.GetDevice())) {
        const auto& cfg = m_configManager.GetConfig();
        m_spoutOutput.SetSenderName(cfg.spoutSenderName);
        m_spoutOutput.SetEnabled(cfg.spoutEnabled);
    }

    // Create shader manager
    m_shaderManager = std::make_unique<ShaderManager>(m_renderer);
    m_shaderManager->EnableFileWatching(true);

    // Workspace manager before the window, for the same reason the shader manager is:
    // GetWorkspaceManager() dereferences this pointer and the menu reads it.
    // Initialize handles relative-path resolution internally.
    m_workspaceManager = std::make_unique<WorkspaceManager>();
    m_workspaceManager->Initialize(m_configManager.GetConfig().layoutsDirectory);

    // Create the window. Everything its panels read on construction — the renderer,
    // the shader manager, the workspace manager, Spout — now exists. The preset list is
    // still empty at this point; the RefreshAll() at the end of this function is what
    // populates the panels once the presets are loaded.
    m_mainWindow = std::make_unique<MainWindow>(*this);
    m_mainWindow->show();

    // Viewport's own swap chain, from the renderer's device. Must run after show():
    // it needs a realised native handle from winId().
    m_mainWindow->Viewport()->CreateSwapChain();

    // Compile the presets the worker read and parsed above. AddPresets compiles the whole
    // batch across all cores, which is what keeps a cold bytecode cache from serialising
    // ~45 D3DCompile calls into several seconds.
    try {
        m_shaderManager->AddPresets(presetJob.get());
    } catch (const std::exception&) {
        // A preset that could not be read is one the directory scan below picks up again.
    }

    // Resolve the shader directory: if the configured path doesn't exist, try the
    // directory next to the executable (works for dev builds run from build/Release/).
    {
        auto& shaderDir = m_configManager.GetConfig().shaderDirectory;
        if (!std::filesystem::exists(shaderDir)) {
            char exePath[MAX_PATH];
            GetModuleFileNameA(nullptr, exePath, MAX_PATH);
            auto altDir = std::filesystem::path(exePath).parent_path() / "shaders";
            if (std::filesystem::exists(altDir)) {
                shaderDir = altDir.string();
            }
        }
    }

    // Scan shader directory
    m_shaderManager->ScanDirectory(m_configManager.GetConfig().shaderDirectory);

    // Create shaders directory if it doesn't exist
    std::filesystem::create_directories(m_configManager.GetConfig().shaderDirectory);

    // OpenVideo flushes the audio player, so the device must have finished opening first.
    audioJob.get();

    // Open last video if available
    if (!m_configManager.GetConfig().lastOpenedVideo.empty()) {
        OpenVideo(m_configManager.GetConfig().lastOpenedVideo);
    }

    // Apply audio DSP settings from config
    m_audioAnalyzer.UpdateSettings(m_configManager.GetConfig().audio);

    m_lastFrameTime = std::chrono::steady_clock::now();

    // Populate the editor, library and parameters panels from the state just restored
    // above; without this the editor opens empty and the parameters panel unpopulated
    // even when an active preset carried over. Placed last, alongside OnParamChanged()
    // below, since both need the fully-restored ShaderManager/workspace state above.
    m_mainWindow->RefreshAll();

    // Upload initial param values to GPU if a preset is already active
    OnParamChanged();

    return true;
}

void Application::Shutdown() {
    // Idempotent: the crash filter calls this and so does the destructor, and everything
    // below is written to run once. Without the guard the second pass reached SaveConfig
    // with m_shaderManager already reset.
    if (m_shutdownDone) return;
    m_shutdownDone = true;

    StopRecording();
    SaveConfig();

    m_audioPlayer.Shutdown();
    m_spoutOutput.Shutdown();

    // Before m_renderer.Shutdown(): the window owns the viewport, the viewport owns a
    // swap chain created from the renderer's device, and a swap chain must not outlive
    // the device it came from. Destroying the window here also takes every panel with
    // it, so nothing is left holding a reference into the components released below.
    m_mainWindow.reset();

    m_shaderManager.reset();
    m_renderer.Shutdown();
    m_decoder.Close();
}

// What the unhandled-exception filter is allowed to do. Releasing the Spout sender takes
// it out of every receiver's list immediately, which is the one piece of state that
// outlives this process. Nothing else: a crashed process must not be writing config.json
// or tearing down a Qt widget tree.
void Application::CrashCleanup() {
    m_spoutOutput.Shutdown();
}

void Application::HandleKeyboardShortcuts(UINT vkCode) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

    // The exact set held, not a subset test. Every binding comparison below is against
    // this: the old form only checked that each *required* modifier was down, so a preset
    // bound to plain P also fired on Ctrl+P and Shift+P, shadowing anything bound to
    // those, and FindBindingConflict called both free.
    const int mods = (ctrl ? MOD_CONTROL : 0) | (shift ? MOD_SHIFT : 0) | (alt ? MOD_ALT : 0);

    // Global shortcuts. Each block is gated on the exact modifier set it wants and falls
    // through to the binding search otherwise, rather than returning unconditionally.
    if (mods == 0) {
        switch (vkCode) {
        case VK_SPACE:
            TogglePlayback();
            return;
        case VK_ESCAPE:
            m_shaderManager->SetPassthrough();
            // The library and parameters panels are retained widgets, so a preset change
            // made from outside them has to be pushed; the Shader menu's Reset to
            // Passthrough item does exactly this.
            if (m_mainWindow) {
                m_mainWindow->RefreshLibrary();
                m_mainWindow->RefreshParameters();
            }
            return;
        case VK_F1:
            if (m_mainWindow) m_mainWindow->ToggleEditorDock();
            return;
        case VK_F2:
            if (m_mainWindow) m_mainWindow->ToggleLibraryDock();
            return;
        case VK_F3:
            if (m_mainWindow) m_mainWindow->ToggleTransportDock();
            return;
        case VK_F4:
            if (m_mainWindow) m_mainWindow->ToggleRecordingDock();
            return;
        case VK_F5:
            // Through the window, not straight into CompileCurrentShader: the editor owns
            // the document being compiled and shows the result in its own status line.
            if (m_mainWindow) m_mainWindow->CompileShader();
            return;
        case VK_F6:
            if (m_mainWindow) m_mainWindow->ShowKeybindingsReference();
            return;
        case VK_F7:
            ToggleVideoOutputWindow();
            return;
        case VK_F8:
            if (m_mainWindow) m_mainWindow->ToggleSpoutDock();
            return;
        case VK_F9:
            // Through the panel, which owns the RecordingSettings the user configured. Calling
            // StartRecording from here with a fresh struct wrote to a hardcoded output.mp4 and
            // ignored every field in the dock.
            if (m_mainWindow) m_mainWindow->ToggleRecording();
            return;
        }
    }

    if (mods == MOD_CONTROL) {
        switch (vkCode) {
        case 'O':
            OpenVideoDialog();
            return;
        case 'S':
            if (m_mainWindow) {
                SaveCurrentShader(m_mainWindow->EditorSource().toStdString());
            }
            return;
        case 'N':
            // Reserved by FindBindingConflict and shown in the F6 reference, but the
            // dispatch had no case for it, so Ctrl+N was unbindable and did nothing.
            if (m_mainWindow) m_mainWindow->OnNewShader();
            return;
        }
    }

    // Custom passthrough keybinding (Escape is always hardcoded; this is a secondary binding)
    {
        const AppConfig& cfg = m_configManager.GetConfig();
        if (cfg.passthroughKey != 0) {
            if (mods == cfg.passthroughModifiers && vkCode == static_cast<UINT>(cfg.passthroughKey)) {
                m_shaderManager->SetPassthrough();
                if (m_mainWindow) {
                    m_mainWindow->RefreshLibrary();
                    m_mainWindow->RefreshParameters();
                }
                return;
            }
        }
    }

    // Check shader keybindings
    for (int i = 0; i < m_shaderManager->GetPresetCount(); ++i) {
        auto* preset = m_shaderManager->GetPreset(i);
        if (!preset || preset->shortcutKey == 0) continue;

        if (mods == preset->shortcutModifiers && vkCode == static_cast<UINT>(preset->shortcutKey)) {
            m_shaderManager->SetActivePreset(i);
            OnParamChanged();
            if (m_mainWindow) {
                // RefreshAll covers all three the old path did by hand: it rebuilds the
                // parameters panel (which clears the keyframe selection), reloads the
                // editor document, and re-marks the library's active row.
                m_mainWindow->RefreshAll();
                m_mainWindow->ShowToast(
                    QString::fromStdString("Switched to: " + preset->name));
            }
            return;
        }
    }

    // Check workspace preset keybindings (skip index 0 = Default, it has no shortcut)
    for (int i = 1; i < m_workspaceManager->GetPresetCount(); ++i) {
        const WorkspacePreset& wp = m_workspaceManager->GetPresets()[i];
        if (wp.shortcutKey == 0) continue;

        if (mods == wp.shortcutModifiers && vkCode == static_cast<UINT>(wp.shortcutKey)) {
            LoadWorkspacePreset(i);
            return;
        }
    }
}

void Application::RequestExit() {
    if (m_exitRequested) return;
    m_exitRequested = true;
    // quit() unwinds the event loop without closing any widget, so MainWindow::closeEvent
    // does not run on this route and the dock layout would go unsaved.
    if (m_mainWindow) m_mainWindow->SaveWindowState();
    QCoreApplication::quit();
}

// One frame, driven by WinMain's zero-interval QTimer. quit() only takes effect when
// the event loop next unwinds, so the timer can still fire after the exit request —
// and a frame rendered into a window that is on its way out is a frame into freed
// swap chain state.
bool Application::TickOnce() {
    if (m_exitRequested) return false;
    SP_PROFILE(kTick);
    ProcessFrame();
    return RenderFrame();
}

MainWindow* Application::GetMainWindow() const {
    return m_mainWindow.get();
}

void Application::ProcessFrame() {
    SP_PROFILE(kProcessFrame);
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - m_lastFrameTime).count();

    m_newVideoFrame = false;

    // Check for shader file changes. A reload replaces the active preset's parameter
    // vector, so the panel's rows have to be rebuilt against it; nothing did that before
    // and the widgets stayed bound to a parameter list the file no longer declares.
    if (m_shaderManager->CheckForChanges() && m_mainWindow)
        m_mainWindow->RefreshParameters();

    if (m_decoder.IsOpen()) {
        if (m_playbackState == PlaybackState::Playing) {
            if (m_decoder.IsLiveCapture()) {
                // Live capture: non-blocking decode on every tick; advance wall-clock time.
                // DecodeNextFrame returns false immediately (EAGAIN) when the device has no
                // new frame yet — we just keep the last frame displayed until the next one.
                const float dt = static_cast<float>(std::min(elapsed, 0.1));
                m_generativeTime += dt;
                m_playbackTime = m_generativeTime;
                m_lastFrameTime = now;

                if (m_decoder.DecodeNextFrame(m_currentFrame)) {
                    m_newVideoFrame = true;
                    m_videoUploadPending = true;
                }
            } else {
                // Video file mode. Two paces, and the difference is the whole of what a
                // record does to playback: normally a frame is decoded when its wall-clock
                // slot arrives and the video loops at the end, but while rendering to file
                // exactly one frame is decoded per tick and the end of stream closes the
                // recording. A render paced off the clock would drop or repeat frames
                // whenever the shader cost more than a frame's time, and a render that
                // looped would write the video twice.
                if (m_offlineRender || elapsed >= m_frameDuration) {
                    if (m_decoder.DecodeNextFrame(m_currentFrame)) {
                        m_newVideoFrame = true;
                        m_videoUploadPending = true;
                        m_playbackTime = static_cast<float>(m_currentFrame.timestamp);
                    } else if (m_offlineRender) {
                        FinishOfflineRender(true);
                        return;   // the decoder has been rewound; nothing left to do here
                    } else {
                        // End of video, loop
                        m_decoder.SeekToTime(0.0);
                        m_audioAnalyzer.Reset();
                        m_audioPlayer.Flush();
                    }
                    m_lastFrameTime = now;
                }

                // Audio: fill the ring buffer to a 2-second target on every tick.
                // ReadAudioAhead decodes audio eagerly and queues video packets for
                // DecodeNextFrame. Submission is capped at the deficit so the ring
                // buffer never fills beyond the target — avoiding the bug where
                // draining at 60 fps submits audio 20x faster than real time,
                // exhausting the ring buffer capacity in < 1 second.
                if (m_decoder.HasAudio() && m_offlineRender) {
                    // The render runs faster than real time, so there is nothing to play:
                    // the speakers are silent and the analyser is fed from the video's own
                    // clock instead. Feeding it per tick would let an audio-reactive shader
                    // see bands from the wrong part of the track; deriving the count from
                    // the frame timestamp keeps the two locked together for the whole file.
                    const int rate = m_decoder.GetAudioSampleRate();
                    const int64_t target =
                        static_cast<int64_t>(static_cast<double>(m_playbackTime) * rate);
                    int64_t need = target - m_offlineAudioFed;

                    constexpr int kAudioBuf = 16384;
                    static float audioBuf[kAudioBuf];

                    if (need > 0) {
                        m_decoder.ReadAudioAhead(
                            static_cast<int>(std::min<int64_t>(need, kAudioBuf * 8)));
                        while (need > 0) {
                            int got = m_decoder.DrainAudioSamples(
                                          audioBuf,
                                          static_cast<int>(std::min<int64_t>(need, kAudioBuf)));
                            if (got <= 0) break;   // audio ran out before the video did
                            m_audioAnalyzer.FeedSamples(audioBuf, got, 1, rate);
                            m_offlineAudioFed += got;
                            need -= got;
                        }
                    }

                    PumpRecordingAudio();
                } else if (m_decoder.HasAudio()) {
                    const int rate        = m_decoder.GetAudioSampleRate();
                    const int deviceRate  = m_audioPlayer.GetDeviceSampleRate();
                    // 2-second target in device-rate samples (the unit GetBufferedSamples returns)
                    const int targetFill  = (deviceRate > 0 ? deviceRate : rate) * 2;
                    const int currentFill = m_audioPlayer.GetBufferedSamples();
                    const int deficit     = targetFill - currentFill;

                    constexpr int kAudioBuf = 16384;
                    static float audioBuf[kAudioBuf];

                    if (deficit > 0) {
                        // Read ahead exactly what's needed to cover the deficit — no per-tick
                        // cap. If the main loop was throttled (background, low fps), deficit
                        // can be large. ReadAudioAhead returns quickly if m_audioPending
                        // already has enough samples.
                        m_decoder.ReadAudioAhead(deficit);

                        // Drain and submit in chunks until the deficit is satisfied.
                        // A single 16384-sample drain can't cover a large deficit on its own
                        // (e.g., after a 1 fps background stall draining 48000 samples).
                        int remaining = deficit;
                        while (remaining > 0) {
                            int got = m_decoder.DrainAudioSamples(
                                          audioBuf, std::min(remaining, kAudioBuf));
                            if (got <= 0) break;
                            m_audioAnalyzer.FeedSamples(audioBuf, got, 1, rate);
                            m_audioPlayer.Submit(audioBuf, got, rate);
                            remaining -= got;
                        }
                    }

                    // ReadAudioAhead hit audio EOF before filling the target. Loop audio
                    // immediately rather than waiting for video EOF (which is ~2 seconds
                    // later due to the video packet queue). Without this, the ring buffer
                    // drains silently for ~2 seconds before the video loop trigger fires.
                    // SeekToTime resets both audio and video to position 0; we then
                    // immediately refill the ring from position 0 WITHOUT flushing, so
                    // any remaining audio in the ring plays through before new audio follows.
                    if (m_decoder.AudioEOFReached()) {
                        m_decoder.SeekToTime(0.0);   // clears m_audioEOFReached via FlushDecoder
                        m_audioAnalyzer.Reset();
                        int remaining = targetFill;
                        m_decoder.ReadAudioAhead(remaining);
                        while (remaining > 0) {
                            int got = m_decoder.DrainAudioSamples(
                                          audioBuf, std::min(remaining, kAudioBuf));
                            if (got <= 0) break;
                            m_audioAnalyzer.FeedSamples(audioBuf, got, 1, rate);
                            m_audioPlayer.Submit(audioBuf, got, rate);
                            remaining -= got;
                        }
                    }
                }
            }
        }
    } else if (m_playbackState != PlaybackState::Paused) {
        // No video: shader time is wall-clock and runs unless the user paused it. The
        // transport state describes a video that is not open, so requiring Playing here
        // froze every shader activated with nothing loaded — Stopped is where the app
        // starts and where CloseVideo leaves it, so the picture drew and never moved.
        // Pause still stops it, which is the only transport verb that means anything
        // with no footage.
        //
        // While a generative render is running, the step is the output frame's own
        // duration rather than the wall clock: RenderFrame submits exactly one frame per
        // tick, so this is what makes the file's timeline and the shader's animation the
        // same clock. Gated on the encoder still running so an encoder that stopped on its
        // own cannot leave the preview stepping at a fixed rate forever.
        const bool rendering = m_generativeRender && m_encoder.IsRecording();
        const float dt = rendering ? m_renderFrameStep
                                   : static_cast<float>(std::min(elapsed, 0.1));
        m_generativeTime += dt;
        m_playbackTime = m_generativeTime;
        m_newVideoFrame = true;
        m_lastFrameTime = now;
    }
}

void Application::PumpRecordingAudio() {
    if (!m_encoder.IsRecording() || !m_decoder.RecordingTapOn()) return;

    // Drained to exhaustion rather than to a target: every sample the read-ahead pulled in
    // belongs in the file, and the muxer interleaves it against the video by timestamp
    // rather than by arrival. Holding any back would only shorten the track.
    constexpr int kRecBuf = 16384;
    static float recBuf[kRecBuf];

    int got;
    while ((got = m_decoder.DrainRecordingSamples(recBuf, kRecBuf)) > 0) {
        if (!m_encoder.SubmitAudio(recBuf, got)) break;   // encoder is wedged; stop pushing
    }
}

void Application::OnParamChanged() {
    ShaderPreset* preset = m_shaderManager->GetActivePreset();
    if (!preset) return;

    float packed[kCustomFloats] = {};
    ShaderManager::PackParamValues(*preset, packed);
    m_renderer.SetCustomUniforms(packed, kCustomFloats);

    for (const auto& p : preset->params) {
        if (p.type == ShaderParamType::Event && p.values[0] > 0.5f) {
            m_eventResetPending = true;
            break;
        }
    }
}

void Application::EvaluateKeyframes() {
    ShaderPreset* preset = m_shaderManager->GetActivePreset();
    if (!preset) return;

    bool anyChanged = false;

    for (auto& p : preset->params) {
        if (!p.timeline || !p.timeline->enabled) continue;

        int valueCount = 1;
        if (p.type == ShaderParamType::Point2D) valueCount = 2;
        else if (p.type == ShaderParamType::Color) valueCount = 4;

        // For Bool/Long: step interpolation (snap to nearest keyframe, no lerp).
        // Evaluate still returns lerped values; we snap afterwards.
        float interpolated[4] = {};
        if (p.timeline->Evaluate(m_playbackTime, interpolated, valueCount)) {
            bool changed = false;
            for (int i = 0; i < valueCount; ++i) {
                float val = interpolated[i];
                // Step types: snap to 0 or 1 (bool) or round to int (long)
                if (p.type == ShaderParamType::Bool)
                    val = (val >= 0.5f) ? 1.0f : 0.0f;
                else if (p.type == ShaderParamType::Long)
                    val = std::round(val);

                if (p.values[i] != val) {
                    p.values[i] = val;
                    changed = true;
                }
            }
            if (changed) anyChanged = true;
        }
    }

    if (anyChanged) OnParamChanged();
}

bool Application::RenderFrame() {
    // Only when the pixels actually changed. The texture keeps its contents between
    // frames, so re-mapping and row-copying 8 MB of identical 1080p every display frame
    // bought nothing but 0.64 ms and half a gigabyte a second of PCIe traffic.
    {
        SP_PROFILE(kVideoUpload);
        if (m_videoUploadPending && !m_currentFrame.data[0].empty()) {
            m_renderer.UploadVideoFrame(m_currentFrame);
            m_videoUploadPending = false;
        }
    }

    // Set shader uniforms
    m_renderer.SetShaderTime(m_playbackTime);

    // Evaluate keyframe animations at current playback time
    EvaluateKeyframes();

    // Push active preset's blend settings so the compositor shader has current values.
    // Only meaningful when a generative shader is active and video is loaded; harmless otherwise.
    {
        const int activeIdx = m_shaderManager->GetActivePresetIndex();
        if (activeIdx >= 0) {
            const auto& preset = m_shaderManager->GetPresets()[activeIdx];
            m_renderer.SetVideoBlend(preset.blendMode, preset.blendAmount);
        } else {
            m_renderer.SetVideoBlend(0, 0.0f);
        }
    }

    // Push latest audio analysis to GPU (b1 cbuffer + t3 spectrum texture).
    if (m_decoder.HasAudio()) {
        m_audioAnalyzer.GetData(m_audioData);
        m_renderer.SetAudioData(&m_audioData);
    } else {
        m_renderer.SetAudioData(nullptr);
    }

    // Set up the D3D11 pixel-shader pipeline state everything below depends on, and
    // render video+shader to the display texture; every consumer blits from there
    {
        SP_PROFILE(kRender);
        m_renderer.BeginFrame();
        m_renderer.RenderToDisplay();
    }

    // Blit processed output to the detached video window (if open)
    if (m_videoOutputWindow.IsOpen())
        m_videoOutputWindow.BlitAndPresent(m_renderer);

    // Share processed frame via Spout (GPU texture copy; does not block the pipeline)
    m_spoutOutput.SendFrame(m_renderer.GetDisplayTexture());

    // Capture the recording frame here: after RenderToDisplay, so the display texture
    // holds this frame's processed picture, and before any later BeginFrame that would
    // move the pipeline on. Only capture on new video frames, to match the encoder's
    // configured framerate.
    if (m_encoder.IsRecording() && m_newVideoFrame) {
        if (m_renderer.RenderToTexture()) {
            std::vector<uint8_t> frameData;
            int width, height;
            if (m_renderer.CopyRenderTargetToStaging(frameData, width, height)) {
                m_encoder.SubmitFrame(frameData, width, height);
            }
        }
        // RenderToTexture changes the active RT and viewport; put the pipeline back
        m_renderer.BeginFrame();
    }

    // Reset event params after they have been visible for one frame
    if (m_eventResetPending) {
        m_eventResetPending = false;
        ShaderPreset* preset = m_shaderManager->GetActivePreset();
        if (preset) {
            for (auto& p : preset->params) {
                if (p.type == ShaderParamType::Event)
                    p.values[0] = 0.0f;
            }
            float packed[kCustomFloats] = {};
            ShaderManager::PackParamValues(*preset, packed);
            m_renderer.SetCustomUniforms(packed, kCustomFloats);
        }
    }

    // The window's frame: the viewport presents the display texture on its own swap
    // chain (which is where this loop's vsync pacing comes from), and the live meters
    // move. Last, so everything above has already produced this frame's picture.
    return m_mainWindow ? m_mainWindow->Tick() : false;
}

bool Application::OpenVideo(const std::string& filepath) {
    StopRecording();   // see CloseVideo
    // Reset prior state before opening — mirrors OpenCapture and prevents stale audio,
    // playback time, and renderer video dimensions when replacing an already-open video.
    m_playbackState  = PlaybackState::Stopped;
    m_playbackTime   = 0.0f;
    m_generativeTime = 0.0f;
    m_currentFrame   = VideoFrame{};
    m_audioAnalyzer.Reset();
    m_audioPlayer.Flush();
    m_renderer.ReleaseVideoTexture();

    if (!m_decoder.Open(filepath)) {
        Toast(m_mainWindow.get(), "Failed to open video: " + filepath);
        return false;
    }

    // D3 guarantees a positive rate, but this is the divisor that decides whether the
    // video advances at all, so it does not take that on trust.
    m_frameDuration = (m_decoder.GetFPS() > 0.0) ? 1.0 / m_decoder.GetFPS() : 1.0 / 30.0;
    m_configManager.GetConfig().lastOpenedVideo = filepath;

    // Decode first frame
    if (m_decoder.DecodeNextFrame(m_currentFrame)) m_videoUploadPending = true;
    m_playbackState = PlaybackState::Paused;

    Toast(m_mainWindow.get(),
          "Opened: " + std::filesystem::path(filepath).filename().string());
    return true;
}

void Application::CloseVideo() {
    // Before Stop(), which a running render ignores, and before the decoder goes: with no
    // file open ProcessFrame takes the generative branch and would never reach the end of
    // stream that closes the encoder.
    StopRecording();
    Stop();
    m_audioPlayer.Flush();
    m_decoder.Close();
    m_currentFrame = VideoFrame{};
    m_audioAnalyzer.Reset();
    // Reset renderer video dimensions so RenderToTexture/RenderToDisplay fall back
    // to generative resolution. Without this, the stale m_videoWidth/Height causes
    // the render target to be sized at the old video resolution, producing a tiny
    // squished image in the top-left of the recording frame with green fill elsewhere.
    m_renderer.ReleaseVideoTexture();
}

void Application::OpenVideoDialog() {
    const QString filepath = QFileDialog::getOpenFileName(
        m_mainWindow.get(), QObject::tr("Open Video"), QString(),
        QStringLiteral("Video Files (*.mp4 *.mov *.avi *.mkv *.webm *.mxf);;All Files (*)"));
    if (filepath.isEmpty()) return;  // cancelled

    OpenVideo(filepath.toStdString());
}

bool Application::OpenCapture(const std::string& deviceOrUrl, bool isDshow) {
    StopRecording();   // see CloseVideo
    Stop();
    m_generativeTime = 0.0f;

    if (!m_decoder.OpenCapture(deviceOrUrl, isDshow)) {
        Toast(m_mainWindow.get(), "Failed to open capture: " + deviceOrUrl);
        return false;
    }

    // D3 guarantees a positive rate, but this is the divisor that decides whether the
    // video advances at all, so it does not take that on trust.
    m_frameDuration = (m_decoder.GetFPS() > 0.0) ? 1.0 / m_decoder.GetFPS() : 1.0 / 30.0;
    m_playbackState = PlaybackState::Playing;
    m_lastFrameTime = std::chrono::steady_clock::now();

    Toast(m_mainWindow.get(), "Live: " + deviceOrUrl);
    return true;
}

void Application::Play() {
    m_playbackState = PlaybackState::Playing;
    m_lastFrameTime = std::chrono::steady_clock::now();
}

void Application::Pause() {
    m_playbackState = PlaybackState::Paused;
    m_audioPlayer.Flush();
}

void Application::Stop() {
    // A render owns the transport until it finishes or is cancelled; a stop that only
    // halted playback would leave the encoder open with nothing arriving at it.
    if (m_offlineRender) return;
    m_playbackState = PlaybackState::Stopped;
    m_audioPlayer.Flush();
    if (m_decoder.IsOpen()) {
        m_decoder.SeekToTime(0.0);
        if (m_decoder.DecodeNextFrame(m_currentFrame)) m_videoUploadPending = true;
    }
    m_playbackTime = 0.0f;
    m_generativeTime = 0.0f;
}

void Application::TogglePlayback() {
    if (m_offlineRender) return;   // see Stop()
    if (m_playbackState == PlaybackState::Playing) {
        Pause();
    } else {
        Play();
    }
}

void Application::SeekTo(double seconds) {
    if (m_offlineRender) return;   // see Stop()
    if (m_decoder.IsOpen()) {
        m_audioPlayer.Flush();
        m_decoder.SeekToTime(seconds);
        if (m_decoder.DecodeNextFrame(m_currentFrame)) m_videoUploadPending = true;
        m_playbackTime = static_cast<float>(seconds);
        m_audioAnalyzer.Reset();
    }
}

void Application::SetAudioVolume(float vol) {
    m_configManager.GetConfig().audioVolume = vol;
    m_audioPlayer.SetVolume(vol);
}

void Application::SetAudioMute(bool mute) {
    m_configManager.GetConfig().muteAudio = mute;
    m_audioPlayer.SetMute(mute);
}

bool Application::CompileCurrentShader(const std::string& source, bool quiet) {
    int activeIndex = m_shaderManager->GetActivePresetIndex();
    auto* preset = m_shaderManager->GetActivePreset();
    if (preset) {
        // Update source in the stored preset, then recompile via index so the
        // compiled ID3D11PixelShader is reliably written into m_compiledShaders.
        preset->source = source;
        if (m_shaderManager->RecompilePreset(activeIndex)) {
            m_shaderManager->SetActivePreset(activeIndex);
            OnParamChanged();
            // A recompile can add, remove or retype parameters, so the panel is rebuilt
            // rather than merely having its keyframe selection cleared. The editor
            // document is deliberately left alone: it is the source just compiled.
            if (m_mainWindow) m_mainWindow->RefreshParameters();
            if (!quiet) Toast(m_mainWindow.get(), "Shader compiled successfully");
            return true;
        } else {
            if (!quiet) Toast(m_mainWindow.get(), "Shader compilation failed: " +
                (preset->compileError.empty() ? "unknown error" : preset->compileError.substr(0, 80)));
            return false;
        }
    } else {
        // No active preset — compile the editor content into a new preset.
        ShaderPreset newPreset;
        newPreset.name = "Untitled";
        newPreset.source = source;
        // AddPreset compiles and stores the shader; no double-compile needed.
        int idx = m_shaderManager->AddPreset(newPreset);
        auto* added = m_shaderManager->GetPreset(idx);
        if (added && added->isValid) {
            m_shaderManager->SetActivePreset(idx);
            OnParamChanged();
            // A preset was added as well as activated, so the library needs rebuilding
            // too. The editor already holds this source; nothing reloads it.
            if (m_mainWindow) {
                m_mainWindow->RefreshLibrary();
                m_mainWindow->RefreshParameters();
            }
            if (!quiet) Toast(m_mainWindow.get(), "Shader compiled successfully");
            return true;
        } else {
            m_shaderManager->RemovePreset(idx);
            if (!quiet) Toast(m_mainWindow.get(), "Shader compilation failed");
            return false;
        }
    }
}

bool Application::SaveCurrentShader(const std::string& source) {
    auto* preset = m_shaderManager->GetActivePreset();
    if (!preset) {
        SaveShaderAsDialog(source);
        return true;
    }

    preset->source = source;

    if (!preset->filepath.empty()) {
        std::ofstream file(preset->filepath);
        if (file.is_open()) {
            file << source;
            Toast(m_mainWindow.get(), "Shader saved: " + preset->name);
            return true;
        }
    }

    SaveShaderAsDialog(source);
    return true;
}

void Application::SaveShaderAsDialog(const std::string& source) {
    const QString chosen = QFileDialog::getSaveFileName(
        m_mainWindow.get(), QObject::tr("Save Shader"), QString(),
        QStringLiteral("HLSL Shader (*.hlsl);;All Files (*)"));
    if (chosen.isEmpty()) return;  // cancelled

    std::string filepath = chosen.toStdString();
    // Win32's OFN_lpstrDefExt auto-appended ".hlsl" when the user typed no extension;
    // QFileDialog::getSaveFileName has no equivalent, so replicate it here.
    if (std::filesystem::path(filepath).extension().empty()) {
        filepath += ".hlsl";
    }

    std::ofstream file(filepath);
    if (file.is_open()) {
        file << source;

        // Update or create preset
        auto* preset = m_shaderManager->GetActivePreset();
        if (preset) {
            preset->filepath = filepath;
            preset->name = std::filesystem::path(filepath).stem().string();
        } else {
            ShaderPreset newPreset;
            newPreset.filepath = filepath;
            newPreset.name = std::filesystem::path(filepath).stem().string();
            newPreset.source = source;
            int idx = m_shaderManager->AddPreset(newPreset);
            m_shaderManager->SetActivePreset(idx);
            OnParamChanged();
            if (m_mainWindow) m_mainWindow->RefreshParameters();
        }

        // Either branch renamed or added a preset, so the library's rows are stale.
        if (m_mainWindow) m_mainWindow->RefreshLibrary();
        Toast(m_mainWindow.get(), "Shader saved: " + filepath);
    }
}

void Application::ScanFolderDialog() {
    const QString dir = QFileDialog::getExistingDirectory(
        m_mainWindow.get(), QObject::tr("Select Shader Folder"), QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) return;  // cancelled

    const std::string path = dir.toStdString();
    m_configManager.GetConfig().shaderDirectory = path;
    m_shaderManager->ScanDirectory(path);
    // Persisted now rather than left to Shutdown()'s save. Choosing a shader folder is an
    // explicit preference, and every other one in the product (noise settings, audio
    // settings, blend mode) writes immediately; leaving this one to a clean
    // exit means an abnormal termination silently reverts it to the previous folder.
    SaveConfig();
    // The scan is the only thing that adds presets in bulk, and the library
    // is a retained list: without this the new shaders are loaded but unlisted.
    // RefreshParameters is not optional either: AddPresets push_backs, which reallocates
    // m_presets, and ParamsPanel and every KeyframeDetail under it hold a ShaderPreset*
    // into that vector which they dereference on the next frame.
    if (m_mainWindow) {
        m_mainWindow->RefreshLibrary();
        m_mainWindow->RefreshParameters();
    }
    Toast(m_mainWindow.get(),
          "Scanned: " + std::filesystem::path(path).filename().string());
}

std::string Application::OpenRecordingOutputDialog(const std::string& currentPath) {
    const QString chosen = QFileDialog::getSaveFileName(
        m_mainWindow.get(), QObject::tr("Choose Recording Output"),
        QString::fromStdString(currentPath),
        QStringLiteral("MP4 Video (*.mp4);;MOV Video (*.mov);;All Files (*)"));
    if (chosen.isEmpty()) return {};  // cancelled

    std::string path = chosen.toStdString();
    // Win32's OFN_lpstrDefExt auto-appended ".mp4" when the user typed no extension;
    // QFileDialog::getSaveFileName has no equivalent, so replicate it here.
    if (std::filesystem::path(path).extension().empty()) {
        path += ".mp4";
    }
    return path;
}

bool Application::StartRecording(const RecordingSettings& settings) {
    int recW, recH;
    double recFPS;

    // An opened file is a render, not a capture. The user asked for that video with this
    // shader on it, which means every frame of it from the first, so the transport is
    // driven from here rather than left for the user to start by hand.
    const bool fileSource = m_decoder.IsOpen() && !m_decoder.IsLiveCapture();
    // A live device is the one source that cannot be rendered: it produces frames at its
    // own pace and stalling it loses them for good. Everything else is a render.
    const bool liveCapture = m_decoder.IsOpen() && m_decoder.IsLiveCapture();

    m_generativeRender = false;

    if (m_decoder.IsOpen()) {
        recW   = m_decoder.GetWidth();
        recH   = m_decoder.GetHeight();
        // The open video's own rate, always. Its frames are that video's frames, so the
        // panel's fps has nothing to say here and the encoder no longer second-guesses it.
        recFPS = m_decoder.GetFPS();
    } else {
        // Generative mode: use configured generative resolution, and the panel's rate.
        // A generative shader has no source rate to inherit, so 0 (a config written before
        // the panel had the control) reads as the default rather than as a rate.
        recW   = m_renderer.GetGenerativeWidth();
        recH   = m_renderer.GetGenerativeHeight();
        recFPS = (settings.fps > 0) ? static_cast<double>(settings.fps) : kDefaultRenderFps;

        // Ensure the generative render loop is running
        if (m_playbackState != PlaybackState::Playing)
            Play();
    }

    // Only a file has audio to mux: a dshow capture device is video-only here, and a
    // generative shader has no source at all.
    const int audioRate = (fileSource && m_decoder.HasAudio())
                              ? m_decoder.GetAudioSampleRate() : 0;

    if (!m_encoder.StartRecording(settings, recW, recH, recFPS, !liveCapture, audioRate)) {
        Toast(m_mainWindow.get(), "Failed to start recording");
        return false;
    }

    if (fileSource) {
        // Rewind and play. No frame is decoded here: ProcessFrame decodes the first one on
        // the next tick, which is what puts it in front of the encoder. Decoding it now
        // would show it and then overwrite it before the capture in RenderFrame ever saw
        // it, and the file would start one frame late.
        m_offlineRender   = true;
        m_offlineAudioFed = 0;
        // Only when the file actually got an audio stream: a codec the container refused
        // leaves HasAudioStream false, and the tap would then decode into nothing.
        m_decoder.SetRecordingTap(m_encoder.HasAudioStream());
        m_audioPlayer.Flush();
        m_audioAnalyzer.Reset();
        m_decoder.SeekToTime(0.0);
        m_playbackTime = 0.0f;
        Play();
        Toast(m_mainWindow.get(), "Rendering to " + settings.outputPath);
    } else if (!liveCapture) {
        // A generative render, and the same bargain the file render above makes: shader
        // time advances by exactly one frame per submitted frame instead of by the wall
        // clock, so the file is written at recFPS whatever the display refreshes at and
        // whatever the shader costs. Without this the tick rate set the frame rate and the
        // declared rate only relabelled it, so a 60 Hz display writing a 25 fps file
        // produced 2.4x the frames and a file 2.4x too long at 0.4x speed.
        //
        // The preview stops being real-time for the duration, which is what makes the
        // output correct: a shader too heavy to hit recFPS now takes longer than real time
        // to render rather than quietly writing a slower file. Nothing can desync against
        // it, because a generative recording muxes no audio at all.
        m_generativeRender = true;
        m_renderFrameStep  = static_cast<float>(1.0 / recFPS);
        Toast(m_mainWindow.get(), "Rendering to " + settings.outputPath);
    } else {
        Toast(m_mainWindow.get(), "Recording started: " + settings.outputPath);
    }
    return true;
}

void Application::StopRecording() {
    if (!m_encoder.IsRecording()) return;

    if (m_offlineRender) {
        FinishOfflineRender(false);
        return;
    }

    const bool wasRender = m_generativeRender;
    m_generativeRender = false;

    // Non-blocking: signals the encoder thread to drain its queue and exit.
    // Flush, file close, and resource free all happen on that thread.
    m_encoder.StopRecording();
    Toast(m_mainWindow.get(), wasRender ? "Render stopped" : "Recording stopped");
}

void Application::FinishOfflineRender(bool completed) {
    // Before StopRecording: the last video frame's own audio is still in the tap, and the
    // encoder stops accepting submissions the moment it is told to drain.
    PumpRecordingAudio();
    m_decoder.SetRecordingTap(false);

    m_offlineRender    = false;
    m_offlineAudioFed  = 0;
    m_generativeRender = false;   // a file render never sets it; cleared so no path can leak it

    m_encoder.StopRecording();

    // Back to the first frame with the transport idle. Leaving it playing would restart
    // the video at real time the instant the file closed, which is the confusion this
    // whole path exists to remove.
    m_playbackState = PlaybackState::Stopped;
    m_audioPlayer.Flush();
    m_audioAnalyzer.Reset();
    if (m_decoder.IsOpen()) {
        m_decoder.SeekToTime(0.0);
        if (m_decoder.DecodeNextFrame(m_currentFrame)) m_videoUploadPending = true;
    }
    m_playbackTime = 0.0f;

    Toast(m_mainWindow.get(), completed ? "Render complete" : "Render cancelled");
}

void Application::SaveConfig() {
    // Update shader presets in config
    auto& config = m_configManager.GetConfig();

    // Null when InitializeQt failed before the manager existed, and again on a second
    // Shutdown. Leaving the saved preset list untouched is right in both cases: clearing
    // it would erase 45 presets' values and keybindings from config.json.
    if (!m_shaderManager) {
        m_configManager.Save(ConfigManager::GetDefaultConfigPath());
        return;
    }

    config.shaderPresets.clear();

    for (int i = 0; i < m_shaderManager->GetPresetCount(); ++i) {
        auto* preset = m_shaderManager->GetPreset(i);
        if (preset && !preset->filepath.empty()) {
            config.shaderPresets.push_back(*preset);
        }
    }

    m_configManager.Save(ConfigManager::GetDefaultConfigPath());
}

void Application::ToggleVideoOutputWindow() {
    if (m_videoOutputWindow.IsOpen())
        m_videoOutputWindow.Close();
    else
        m_videoOutputWindow.Open(m_renderer.GetDevice(), m_renderer.GetContext());
}

void Application::SetSpoutEnabled(bool enabled) {
    m_configManager.GetConfig().spoutEnabled = enabled;
    m_spoutOutput.SetEnabled(enabled);
    SaveConfig();
}

void Application::SetSpoutSenderName(const std::string& name) {
    m_configManager.GetConfig().spoutSenderName = name;
    m_spoutOutput.SetSenderName(name);
    SaveConfig();
}

void Application::UpdateAudioSettings() {
    m_audioAnalyzer.UpdateSettings(m_configManager.GetConfig().audio);
    SaveConfig();
}

void Application::RegenerateNoise() {
    const auto& n = m_configManager.GetConfig().noise;
    m_renderer.UpdateNoiseTexture(n.scale, n.textureSize);
    SaveConfig();
}

void Application::ApplyGenerativeResolution() {
    const auto& cfg = m_configManager.GetConfig();
    m_renderer.SetGenerativeResolution(cfg.generativeWidth, cfg.generativeHeight);
    SaveConfig();
}

std::string Application::GetKeyName(int vkCode) const {
    if (vkCode >= 'A' && vkCode <= 'Z') {
        return std::string(1, static_cast<char>(vkCode));
    }
    if (vkCode >= '0' && vkCode <= '9') {
        return std::string(1, static_cast<char>(vkCode));
    }
    if (vkCode >= VK_F1 && vkCode <= VK_F12) {
        return "F" + std::to_string(vkCode - VK_F1 + 1);
    }
    return "Key" + std::to_string(vkCode);
}


std::string Application::GetComboName(int vkCode, int modifiers) const {
    std::string result;
    if (modifiers & MOD_CONTROL) result += "Ctrl+";
    if (modifiers & MOD_ALT)     result += "Alt+";
    if (modifiers & MOD_SHIFT)   result += "Shift+";
    result += GetKeyName(vkCode);
    return result;
}

void Application::LoadWorkspacePreset(int index) {
    // Applied here and now. The window restores the dock state, applies the panel
    // visibility and raises the toast; this is the same call the View menu makes.
    if (m_mainWindow) m_mainWindow->LoadWorkspacePreset(index);
}

std::string Application::FindBindingConflict(int vkCode, int modifiers,
                                              int excludeShaderIdx,
                                              int excludeWorkspaceIdx,
                                              bool excludePassthrough) const
{
    if (vkCode == 0) return {};

    // Reserved for every modifier combination, not only the bare key. The dispatch in
    // HandleKeyboardShortcuts matches on the VK code alone, so Shift+F1 fired Toggle
    // Editor and a plain O or S was swallowed by the Ctrl+O / Ctrl+S cases and did
    // nothing at all. Reporting those free made them bindable, and then dead or wrong.
    switch (vkCode) {
    case VK_SPACE:  return "reserved for Play/Pause (Space)";
    case VK_ESCAPE: return "reserved for Reset to Passthrough (Escape)";
    case VK_F1:     return "reserved for Toggle Editor (F1)";
    case VK_F2:     return "reserved for Toggle Library (F2)";
    case VK_F3:     return "reserved for Toggle Transport (F3)";
    case VK_F4:     return "reserved for Toggle Recording (F4)";
    case VK_F5:     return "reserved for Compile (F5)";
    case VK_F6:     return "reserved for Toggle Keybindings (F6)";
    case VK_F7:     return "reserved for Video Output Window (F7)";
    case VK_F8:     return "reserved for Spout Output panel (F8)";
    case VK_F9:     return "reserved for Start/Stop Recording (F9)";
    case 'O':       return "reserved for Open Video (Ctrl+O)";
    case 'S':       return "reserved for Save Shader (Ctrl+S)";
    case 'N':       return "reserved for New Shader (Ctrl+N)";
    }

    // Passthrough keybinding
    if (!excludePassthrough) {
        const AppConfig& cfg = m_configManager.GetConfig();
        if (cfg.passthroughKey != 0 &&
            cfg.passthroughKey == vkCode && cfg.passthroughModifiers == modifiers)
            return "conflicts with (No Effect) keybinding";
    }

    // Shader presets
    const auto& shaderPresets = m_shaderManager->GetPresets();
    for (int i = 0; i < static_cast<int>(shaderPresets.size()); ++i) {
        if (i == excludeShaderIdx) continue;
        const ShaderPreset& p = shaderPresets[i];
        if (p.shortcutKey == 0) continue;
        if (p.shortcutKey == vkCode && p.shortcutModifiers == modifiers)
            return "conflicts with shader \"" + p.name + "\"";
    }

    // Workspace presets
    if (m_workspaceManager) {
        const auto& wps = m_workspaceManager->GetPresets();
        for (int i = 0; i < static_cast<int>(wps.size()); ++i) {
            if (i == excludeWorkspaceIdx) continue;
            if (wps[i].shortcutKey == 0) continue;
            if (wps[i].shortcutKey == vkCode && wps[i].shortcutModifiers == modifiers)
                return "conflicts with workspace \"" + wps[i].name + "\"";
        }
    }

    return {};
}

} // namespace SP
