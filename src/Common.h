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

// Keyframe interpolation
enum class InterpolationMode { Linear, EaseInOut, CubicBezier };

struct BezierHandles {
    float outX = 0.33f, outY = 0.0f;  // control point leaving this keyframe (normalised 0-1)
    float inX  = 0.67f, inY  = 1.0f;  // control point entering next keyframe (normalised 0-1)
};

struct Keyframe {
    float time = 0.0f;              // absolute video time in seconds
    float values[4] = {};           // same layout as ShaderParam::values
    InterpolationMode mode = InterpolationMode::Linear;
    BezierHandles handles;
};

struct KeyframeTimeline {
    bool enabled = false;
    std::vector<Keyframe> keyframes; // sorted by time

    // Evaluate interpolated value at given time. Writes to out[0..valueCount-1].
    // Returns true if a value was written (timeline enabled and non-empty).
    bool Evaluate(float time, float out[4], int valueCount) const;

    // Insert keyframe maintaining sort order by time. Returns index of inserted keyframe.
    int AddKeyframe(const Keyframe& kf);

    // Remove keyframe at index.
    void RemoveKeyframe(int index);
};

enum class ShaderParamType { Float, Bool, Long, Color, Point2D, Event, AudioBand };

struct ShaderParam {
    std::string name;               // HLSL identifier; used for #define alias
    std::string label;              // ImGui display label (defaults to name)
    ShaderParamType type = ShaderParamType::Float;
    float values[4]        = {};    // Current values: float/bool/long/event→[0],
                                    //   color→RGBA, point2d→XY
    float defaultValues[4] = {};    // Restored on "Reset to defaults"
    float min  = 0.0f;
    float max  = 1.0f;
    float step = 0.01f;
    std::vector<std::string> longLabels; // Dropdown labels for type=Long
    std::vector<int>         longValues; // Selectable int values for type=Long (parallel to longLabels)
    int cbufferOffset = 0;          // Float index into custom[16]; set at parse time; -1 for AudioBand
    std::string audioBand;          // For AudioBand: "bass"|"mid"|"high"|"rms"|"beat"|"centroid"
    std::optional<KeyframeTimeline> timeline;  // nullopt until user enables keyframing
};

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
    bool isGenerative = false;  // True if SHADER_TYPE = "generative" in ISF block
    bool isAudio = false;       // True if SHADER_TYPE = "audio" in ISF block
    int   blendMode   = 0;      // 0=Off, 1=Normal, 2=Add, 3=Multiply, 4=Screen,
                                //   5=Overlay, 6=Soft Light, 7=Difference,
                                //   8=Exclusion, 9=Darken, 10=Lighten
    float blendAmount = 1.0f;   // Blend strength [0,1]
    std::string compileError;
    std::vector<ShaderParam> params;
    // Persistence bridge: saved values keyed by param name, restored after re-parse.
    // Format: { "PixelSize": [8.0], "Tint": [1.0, 0.8, 0.6, 1.0] }
    std::unordered_map<std::string, std::vector<float>> savedParamValues;
    std::unordered_map<std::string, KeyframeTimeline> savedKeyframes;
};

struct WorkspacePreset {
    std::string name;
    std::string filepath;        // absolute path to .ini file; empty = built-in Default
    int shortcutKey = 0;         // VK code; 0 = none
    int shortcutModifiers = 0;   // MOD_CONTROL | MOD_SHIFT | MOD_ALT bitmask
    bool showEditor = true;
    bool showLibrary = true;
    bool showTransport = true;
    bool showRecording = false;
    bool showKeybindingsPanel = false;
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

// Live audio analysis output — written by AudioAnalyzer after each FFT pass,
// read by Application each render frame.
struct AudioData {
    float rms             = 0.0f;  // Overall RMS [0,1]
    float bass            = 0.0f;  // 20-250 Hz [0,1]
    float mid             = 0.0f;  // 250-4000 Hz [0,1]
    float high            = 0.0f;  // 4000-20000 Hz [0,1]
    float beat            = 0.0f;  // Decaying pulse on onset [0,1]
    float spectralCentroid = 0.0f; // Normalised centre of mass [0,1]
    static constexpr int kSpectrumBins = 256;
    float spectrum[kSpectrumBins] = {};  // Normalised per-bin magnitudes [0,1]
};

// DSP tuning — stored in config.json, editable from the Audio Monitor panel.
struct AudioSettings {
    float beatSensitivity = 1.5f;  // Threshold = sensitivity * rolling-average bass energy
    float beatDecay       = 0.92f; // Multiplicative per-frame decay of beat pulse
    float smoothing       = 0.3f;  // EMA coefficient: 0 = no smoothing, 1 = frozen
};

// Noise texture settings — controls the globally-bound t1 noise texture
struct NoiseSettings {
    float scale       = 4.0f;   // frequency multiplier (higher = more repetitions)
    int   textureSize = 512;    // texture dimensions (power of 2)
};

// Application configuration
struct AppConfig {
    std::vector<ShaderPreset> shaderPresets;
    RecordingSettings recordingDefaults;
    bool autoCompileOnSave = true;
    int autoCompileDelayMs = 500;
    std::string lastOpenedVideo;
    std::string shaderDirectory = "shaders";
    std::string layoutsDirectory = "layouts";
    bool timeDisplayFrames = false;  // true = show frame numbers; false = show seconds

    // Noise generator
    NoiseSettings noise;

    // Generative shader output resolution (used when no video is loaded)
    int generativeWidth  = 1920;
    int generativeHeight = 1080;


    // Audio analysis DSP settings
    AudioSettings audio;

    // Spout output
    bool        spoutEnabled    = false;
    std::string spoutSenderName = "ShaderPlayer";

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
