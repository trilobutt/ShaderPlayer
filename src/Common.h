#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>
#include <optional>
#include <filesystem>

using Microsoft::WRL::ComPtr;

namespace SP {

// Forward declarations
class Application;
class VideoDecoder;
class D3D11Renderer;
class ShaderManager;
class VideoEncoder;
class UIManager;
class ConfigManager;

// Frame data structure
struct VideoFrame {
    std::vector<uint8_t> data[4];  // Plane data (Y, U, V, or RGB)
    int linesize[4];
    int width;
    int height;
    int format;  // AVPixelFormat
    int64_t pts;
    double timestamp;  // In seconds
};

// Shader preset structure
struct ShaderPreset {
    std::string name;
    std::string filepath;
    std::string source;
    int shortcutKey = 0;  // Virtual key code
    int shortcutModifiers = 0;  // MOD_CONTROL, MOD_SHIFT, etc.
    bool isValid = false;
    std::string compileError;
};

// Recording settings
struct RecordingSettings {
    std::string outputPath;
    int width = 0;   // 0 = source resolution
    int height = 0;
    int bitrate = 20000000;  // 20 Mbps
    int fps = 0;  // 0 = source fps
    std::string codec = "libx264";  // or "prores_ks"
    std::string preset = "medium";
    int proresProfile = 2;  // 0=proxy, 1=LT, 2=422, 3=HQ
};

// Application configuration
struct AppConfig {
    std::vector<ShaderPreset> shaderPresets;
    RecordingSettings recordingDefaults;
    bool autoCompileOnSave = true;
    int autoCompileDelayMs = 500;
    std::string lastOpenedVideo;
    std::string shaderDirectory = "shaders";
    
    // UI layout
    float editorPanelWidth = 500.0f;
    float libraryPanelHeight = 200.0f;
    bool showEditor = true;
    bool showLibrary = true;
    bool showTransport = true;
};

// Playback state
enum class PlaybackState {
    Stopped,
    Playing,
    Paused
};

// Callback types
using FrameCallback = std::function<void(const VideoFrame&)>;
using CompileCallback = std::function<void(bool success, const std::string& error)>;

// Constants
constexpr int MAX_FRAME_QUEUE_SIZE = 8;
constexpr int ENCODER_QUEUE_SIZE = 16;

} // namespace SP
