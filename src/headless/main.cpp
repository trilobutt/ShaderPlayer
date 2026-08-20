// shaderfx — headless frame runner.
//
// The same D3D11Renderer and ShaderManager the Qt app drives, with stdin/stdout in
// place of a video decoder and a viewport. Nothing about the shader pipeline is
// reimplemented here: the ISF parse, the `#define` preamble, ShaderCommon.hlsli, the
// custom[] packing, the noise texture, the b0/b1/t0/t1/t3 bindings and the
// fullscreen-triangle draw all come from the same objects the app uses, so a shader
// run through this tool renders what it renders in the editor. A second
// implementation would be a second set of bugs, and that is exactly what this file
// exists to avoid.
//
// Frames are raw interleaved 8-bit: rgb24 by default, rgba on request. There is no
// container and no header, so --size is mandatory; the caller (ffmpeg, a Python
// pipeline) already knows the geometry.

#include "Common.h"
#include "D3D11Renderer.h"
#include "ShaderManager.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace SP;

namespace {

// ---------------------------------------------------------------- diagnostics

int Fail(const std::string& msg) {
    std::cerr << "shaderfx: " << msg << "\n";
    return 1;
}

const char* kUsage =
    "shaderfx — run a ShaderPlayer HLSL shader over a raw frame stream.\n"
    "\n"
    "  shaderfx --shader <name|path> --size WxH [options] < in.raw > out.raw\n"
    "  shaderfx --shader <name|path> --list-params\n"
    "  shaderfx --list-shaders\n"
    "\n"
    "Shader selection\n"
    "  --shader <name|path>   Shader to run. A bare name is resolved against the\n"
    "                         shader directory; a path is used as given.\n"
    "  --shader-dir <dir>     Where bare names are looked up. Default: $SHADERPLAYER_SHADER_DIR,\n"
    "                         then ./shaders, then <exe>/shaders, then <exe>/default_shaders.\n"
    "\n"
    "Frames\n"
    "  --size WxH             Frame geometry. Required (a raw stream carries none).\n"
    "  --pix rgb24|rgba       Input and output layout. Default rgb24.\n"
    "  --frames N             Stop after N frames. Default 0 = until stdin ends.\n"
    "  --in <file>            Read frames from a file instead of stdin.\n"
    "  --out <file>           Write frames to a file instead of stdout.\n"
    "  --generative           Render with no input frames (SHADER_TYPE generative).\n"
    "                         --frames is then required.\n"
    "\n"
    "Time\n"
    "  --fps F                Frame rate the `time` uniform advances at. Default 30.\n"
    "  --start T              Value of `time` on the first frame, in seconds. Default 0.\n"
    "                         Use it to place a segment inside a longer piece so\n"
    "                         animated shaders stay continuous across a cut.\n"
    "\n"
    "Parameters\n"
    "  --set Name=v           Override one ISF parameter. Repeatable. A point2d or\n"
    "                         color takes a comma-separated list: --set Tint=1,0.8,0.6,1\n"
    "  --params <file.json>   Overrides as one JSON object: {\"Strength\": 0.04}\n"
    "  --keyframes <file.json>  Animate parameters over time. See CLAUDE.md.\n"
    "  --list-params          Print the shader's parameters as JSON and exit.\n"
    "\n"
    "Pipeline\n"
    "  --noise-scale F        Noise texture frequency (t1). Default 4.\n"
    "  --noise-size N         Noise texture edge, power of two. Default 512.\n"
    "  --blend MODE:AMOUNT    Composite a generative shader over the input frames,\n"
    "                         e.g. --blend 4:0.75 for Screen at 75%.\n"
    "  --audio k=v,...        Fix the audio uniforms (b1) for every frame. Keys:\n"
    "                         rms bass mid high beat centroid.\n"
    "  -v, --verbose          Report progress on stderr.\n";

// ---------------------------------------------------------------- arg helpers

bool ParseSize(const std::string& s, int& w, int& h) {
    const size_t x = s.find_first_of("xX*");
    if (x == std::string::npos) return false;
    try {
        w = std::stoi(s.substr(0, x));
        h = std::stoi(s.substr(x + 1));
    } catch (...) { return false; }
    return w > 0 && h > 0;
}

std::vector<float> ParseFloatList(const std::string& s) {
    std::vector<float> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (tok == "true")  { out.push_back(1.0f); continue; }
        if (tok == "false") { out.push_back(0.0f); continue; }
        try { out.push_back(std::stof(tok)); } catch (...) { return {}; }
    }
    return out;
}

std::filesystem::path ExeDir() {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
}

// Mirrors Application's shaderDirectory fallback: an explicit setting wins, then the
// working directory, then whatever shipped beside the executable.
std::filesystem::path ResolveShaderDir(const std::string& explicitDir) {
    if (!explicitDir.empty()) return explicitDir;
    if (const char* env = std::getenv("SHADERPLAYER_SHADER_DIR"); env && *env)
        return env;
    for (const auto& c : { std::filesystem::path("shaders"),
                           ExeDir() / "shaders",
                           ExeDir() / "default_shaders" }) {
        std::error_code ec;
        if (std::filesystem::is_directory(c, ec) &&
            !std::filesystem::is_empty(c, ec)) return c;
    }
    return ExeDir() / "default_shaders";
}

std::filesystem::path ResolveShaderPath(const std::string& shader,
                                        const std::filesystem::path& dir) {
    std::error_code ec;
    if (shader.find_first_of("/\\") != std::string::npos ||
        std::filesystem::is_regular_file(shader, ec)) return shader;
    for (const char* ext : { ".hlsl", ".fx", ".ps" }) {
        auto p = dir / (shader + ext);
        if (std::filesystem::is_regular_file(p, ec)) return p;
    }
    return dir / (shader + ".hlsl");  // report the expected name in the error
}

// ---------------------------------------------------------------- parameters

const char* TypeName(ShaderParamType t) {
    switch (t) {
    case ShaderParamType::Float:     return "float";
    case ShaderParamType::Bool:      return "bool";
    case ShaderParamType::Long:      return "long";
    case ShaderParamType::Color:     return "color";
    case ShaderParamType::Point2D:   return "point2d";
    case ShaderParamType::Event:     return "event";
    case ShaderParamType::AudioBand: return "audio";
    }
    return "float";
}

int ValueCount(ShaderParamType t) {
    if (t == ShaderParamType::Color)   return 4;
    if (t == ShaderParamType::Point2D) return 2;
    return 1;
}

// An unknown name is an error rather than a warning: a typo that silently does
// nothing costs a full render to notice, and the render is the expensive part.
bool ApplyOverride(ShaderPreset& preset, const std::string& name,
                   const std::vector<float>& vals, std::string& err) {
    for (auto& p : preset.params) {
        if (p.name != name) continue;
        if (p.type == ShaderParamType::AudioBand) {
            err = "'" + name + "' is an audio band; set it with --audio";
            return false;
        }
        const int n = ValueCount(p.type);
        if (static_cast<int>(vals.size()) != n) {
            err = "'" + name + "' takes " + std::to_string(n) + " value(s), got " +
                  std::to_string(vals.size());
            return false;
        }
        for (int i = 0; i < n; ++i) p.values[i] = vals[i];
        return true;
    }
    std::string have;
    for (const auto& p : preset.params) have += (have.empty() ? "" : ", ") + p.name;
    err = "no parameter '" + name + "'; this shader has: " + (have.empty() ? "(none)" : have);
    return false;
}

std::string ListParamsJson(const ShaderPreset& preset) {
    nlohmann::json j;
    j["name"]       = preset.name;
    j["generative"] = preset.isGenerative;
    j["audio"]      = preset.isAudio;
    j["params"]     = nlohmann::json::array();
    for (const auto& p : preset.params) {
        nlohmann::json e;
        e["name"]   = p.name;
        e["label"]  = p.label;
        e["type"]   = TypeName(p.type);
        e["offset"] = p.cbufferOffset;
        if (p.type == ShaderParamType::AudioBand) {
            e["band"] = p.audioBand;
        } else {
            const int n = ValueCount(p.type);
            auto def = nlohmann::json::array();
            for (int i = 0; i < n; ++i) def.push_back(p.defaultValues[i]);
            e["default"] = def;
            e["min"] = p.min; e["max"] = p.max; e["step"] = p.step;
            if (p.type == ShaderParamType::Long) {
                e["values"] = p.longValues;
                e["labels"] = p.longLabels;
            }
        }
        j["params"].push_back(e);
    }
    return j.dump(2);
}

// Keyframe JSON, one entry per animated parameter:
//   { "Strength": { "mode": "easeinout",
//                   "keys": [ {"t": 0.0, "v": 0.01}, {"t": 1.2, "v": 0.06} ] } }
// `t` is absolute time in seconds on the same clock as --start, so a keyframe file
// describes a position in the finished piece rather than in this invocation.
bool LoadKeyframes(const std::string& path, ShaderPreset& preset, std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "cannot open keyframe file: " + path; return false; }
    nlohmann::json j;
    try { f >> j; } catch (const std::exception& e) { err = std::string("keyframes: ") + e.what(); return false; }
    if (!j.is_object()) { err = "keyframes: expected a JSON object"; return false; }

    for (auto it = j.begin(); it != j.end(); ++it) {
        ShaderParam* target = nullptr;
        for (auto& p : preset.params)
            if (p.name == it.key() && p.type != ShaderParamType::AudioBand) target = &p;
        if (!target) { err = "keyframes: no parameter '" + it.key() + "'"; return false; }

        const auto& spec = it.value();
        if (!spec.contains("keys") || !spec["keys"].is_array()) {
            err = "keyframes: '" + it.key() + "' has no keys array"; return false;
        }
        const std::string mode = spec.value("mode", std::string("linear"));
        InterpolationMode im = InterpolationMode::Linear;
        if      (mode == "easeinout") im = InterpolationMode::EaseInOut;
        else if (mode == "bezier")    im = InterpolationMode::CubicBezier;
        else if (mode != "linear")    { err = "keyframes: unknown mode '" + mode + "'"; return false; }

        KeyframeTimeline tl;
        tl.enabled = true;
        for (const auto& k : spec["keys"]) {
            Keyframe kf;
            kf.time = k.value("t", 0.0f);
            kf.mode = im;
            const auto& v = k.at("v");
            if (v.is_array()) {
                for (int i = 0; i < 4 && i < static_cast<int>(v.size()); ++i)
                    kf.values[i] = v[i].get<float>();
            } else {
                kf.values[0] = v.get<float>();
            }
            if (k.contains("handles") && k["handles"].is_array() && k["handles"].size() == 4) {
                kf.handles.outX = k["handles"][0].get<float>();
                kf.handles.outY = k["handles"][1].get<float>();
                kf.handles.inX  = k["handles"][2].get<float>();
                kf.handles.inY  = k["handles"][3].get<float>();
            }
            tl.AddKeyframe(kf);
        }
        target->timeline = std::move(tl);
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------- main

int main(int argc, char** argv) {
    std::string shaderName, shaderDirArg, inPath, outPath, keyframePath, paramsPath;
    std::vector<std::pair<std::string, std::vector<float>>> sets;
    int  width = 0, height = 0, frameCount = 0, channels = 3;
    int  noiseSize = 512, blendMode = 0;
    float fps = 30.0f, startTime = 0.0f, noiseScale = 4.0f, blendAmount = 1.0f;
    bool generative = false, listParams = false, listShaders = false, verbose = false;
    AudioData audio{};
    bool haveAudio = false;

    auto need = [&](int i, const char* what) -> std::string {
        if (i + 1 >= argc) { std::cerr << "shaderfx: " << what << " needs a value\n"; std::exit(1); }
        return argv[i + 1];
    };

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if      (a == "--shader")      { shaderName   = need(i, "--shader");      ++i; }
        else if (a == "--shader-dir")  { shaderDirArg = need(i, "--shader-dir");  ++i; }
        else if (a == "--in")          { inPath       = need(i, "--in");          ++i; }
        else if (a == "--out")         { outPath      = need(i, "--out");         ++i; }
        else if (a == "--params")      { paramsPath   = need(i, "--params");      ++i; }
        else if (a == "--keyframes")   { keyframePath = need(i, "--keyframes");   ++i; }
        else if (a == "--generative")  { generative  = true; }
        else if (a == "--list-params") { listParams  = true; }
        else if (a == "--list-shaders"){ listShaders = true; }
        else if (a == "-v" || a == "--verbose") { verbose = true; }
        else if (a == "-h" || a == "--help") { std::cout << kUsage; return 0; }
        else if (a == "--size") {
            if (!ParseSize(need(i, "--size"), width, height)) return Fail("--size wants WxH, e.g. 1920x1080");
            ++i;
        }
        else if (a == "--pix") {
            const std::string v = need(i, "--pix"); ++i;
            if      (v == "rgb24") channels = 3;
            else if (v == "rgba")  channels = 4;
            else return Fail("--pix wants rgb24 or rgba");
        }
        else if (a == "--frames")      { frameCount = std::atoi(need(i, "--frames").c_str()); ++i; }
        else if (a == "--fps")         { fps        = std::stof(need(i, "--fps"));            ++i; }
        else if (a == "--start")       { startTime  = std::stof(need(i, "--start"));          ++i; }
        else if (a == "--noise-scale") { noiseScale = std::stof(need(i, "--noise-scale"));    ++i; }
        else if (a == "--noise-size")  { noiseSize  = std::atoi(need(i, "--noise-size").c_str()); ++i; }
        else if (a == "--blend") {
            const std::string v = need(i, "--blend"); ++i;
            const size_t c = v.find(':');
            blendMode   = std::atoi(v.substr(0, c).c_str());
            blendAmount = (c == std::string::npos) ? 1.0f : std::stof(v.substr(c + 1));
        }
        else if (a == "--audio") {
            const std::string v = need(i, "--audio"); ++i;
            haveAudio = true;
            std::stringstream ss(v);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                const size_t eq = tok.find('=');
                if (eq == std::string::npos) return Fail("--audio wants key=value pairs");
                const std::string k = tok.substr(0, eq);
                const float val = std::stof(tok.substr(eq + 1));
                if      (k == "rms")      audio.rms   = val;
                else if (k == "bass")     audio.bass  = val;
                else if (k == "mid")      audio.mid   = val;
                else if (k == "high")     audio.high  = val;
                else if (k == "beat")     audio.beat  = val;
                else if (k == "centroid") audio.spectralCentroid = val;
                else return Fail("--audio: unknown key '" + k + "'");
            }
        }
        else if (a == "--set") {
            const std::string v = need(i, "--set"); ++i;
            const size_t eq = v.find('=');
            if (eq == std::string::npos) return Fail("--set wants Name=value");
            auto vals = ParseFloatList(v.substr(eq + 1));
            if (vals.empty()) return Fail("--set " + v + ": value is not a number list");
            sets.emplace_back(v.substr(0, eq), std::move(vals));
        }
        else return Fail("unknown argument '" + a + "' (try --help)");
    }

    const auto shaderDir = ResolveShaderDir(shaderDirArg);

    if (listShaders) {
        std::error_code ec;
        if (!std::filesystem::is_directory(shaderDir, ec))
            return Fail("no shader directory at " + shaderDir.string());
        std::cout << shaderDir.string() << "\n";
        for (const auto& e : std::filesystem::directory_iterator(shaderDir)) {
            const auto ext = e.path().extension().string();
            if (ext == ".hlsl" || ext == ".fx" || ext == ".ps")
                std::cout << "  " << e.path().stem().string() << "\n";
        }
        return 0;
    }

    if (shaderName.empty()) { std::cerr << kUsage; return 1; }
    if (!listParams) {
        if (width == 0 || height == 0) return Fail("--size is required (a raw stream carries no geometry)");
        if (generative && frameCount <= 0) return Fail("--generative needs --frames N (there is no stdin to end)");
    }

    // --list-params needs the ISF block, not a GPU: parse without a device so the
    // tool answers on a machine with no D3D11 (CI, a build box, a remote shell).
    const auto shaderPath = ResolveShaderPath(shaderName, shaderDir);
    if (!std::filesystem::is_regular_file(shaderPath))
        return Fail("no such shader: " + shaderPath.string());

    D3D11Renderer renderer;
    ShaderManager shaders(renderer);
    ShaderPreset preset;

    if (listParams) {
        // LoadShaderMetadataFromFile parses ISF without compiling, so no device is
        // touched on this path.
        if (!shaders.LoadShaderMetadataFromFile(shaderPath.string(), preset))
            return Fail("cannot read " + shaderPath.string());
        std::cout << ListParamsJson(preset) << "\n";
        return 0;
    }

    if (!renderer.Initialize(width, height))
        return Fail("D3D11 device creation failed");
    renderer.SetGenerativeResolution(width, height);
    if (!renderer.UpdateNoiseTexture(noiseScale, noiseSize))
        return Fail("noise texture creation failed");
    renderer.SetAudioData(haveAudio ? &audio : nullptr);
    if (blendMode > 0) renderer.SetVideoBlend(blendMode, blendAmount);

    // Metadata first, overrides second, compile last: AddPreset preserves param values
    // that are already set, so the shader is compiled once with the values it will run
    // with rather than compiled and then patched.
    if (!shaders.LoadShaderMetadataFromFile(shaderPath.string(), preset))
        return Fail("cannot read " + shaderPath.string());

    if (!paramsPath.empty()) {
        std::ifstream f(paramsPath);
        if (!f) return Fail("cannot open " + paramsPath);
        nlohmann::json j;
        try { f >> j; } catch (const std::exception& e) { return Fail(std::string("--params: ") + e.what()); }
        if (!j.is_object()) return Fail("--params: expected a JSON object");
        for (auto it = j.begin(); it != j.end(); ++it) {
            std::vector<float> vals;
            if (it.value().is_array())        for (const auto& v : it.value()) vals.push_back(v.get<float>());
            else if (it.value().is_boolean()) vals.push_back(it.value().get<bool>() ? 1.0f : 0.0f);
            else                              vals.push_back(it.value().get<float>());
            std::string err;
            if (!ApplyOverride(preset, it.key(), vals, err)) return Fail(err);
        }
    }
    for (const auto& [name, vals] : sets) {
        std::string err;
        if (!ApplyOverride(preset, name, vals, err)) return Fail(err);
    }
    if (!keyframePath.empty()) {
        std::string err;
        if (!LoadKeyframes(keyframePath, preset, err)) return Fail(err);
    }

    const int index = shaders.AddPreset(preset);
    ShaderPreset* active = shaders.GetPreset(index);
    if (!active || !active->isValid)
        return Fail("compile failed:\n" + (active ? active->compileError : std::string("(no preset)")));
    shaders.SetActivePreset(index);

    const bool animated = [&] {
        for (const auto& p : active->params) if (p.timeline && p.timeline->enabled) return true;
        return false;
    }();

    // ------------------------------------------------------------------ streams
    std::ifstream inFile;
    std::ofstream outFile;
    if (!inPath.empty()) {
        inFile.open(inPath, std::ios::binary);
        if (!inFile) return Fail("cannot open " + inPath);
    } else {
        _setmode(_fileno(stdin), _O_BINARY);
    }
    if (!outPath.empty()) {
        outFile.open(outPath, std::ios::binary);
        if (!outFile) return Fail("cannot create " + outPath);
    } else {
        _setmode(_fileno(stdout), _O_BINARY);
    }

    const size_t inBytes  = static_cast<size_t>(width) * height * channels;
    std::vector<uint8_t> inBuf(inBytes);
    std::vector<uint8_t> staged;
    std::vector<uint8_t> outBuf(inBytes);

    VideoFrame frame{};
    frame.width  = width;
    frame.height = height;
    frame.linesize[0] = width * 4;
    frame.data[0].resize(static_cast<size_t>(width) * height * 4);

    int done = 0;
    for (int f = 0; frameCount == 0 || f < frameCount; ++f) {
        if (!generative) {
            if (inFile.is_open()) {
                inFile.read(reinterpret_cast<char*>(inBuf.data()), inBytes);
                if (static_cast<size_t>(inFile.gcount()) != inBytes) break;   // clean end of stream
            } else {
                if (std::fread(inBuf.data(), 1, inBytes, stdin) != inBytes) break;
            }

            if (channels == 4) {
                std::memcpy(frame.data[0].data(), inBuf.data(), inBytes);
            } else {
                const uint8_t* s = inBuf.data();
                uint8_t* d = frame.data[0].data();
                for (size_t px = 0, n = static_cast<size_t>(width) * height; px < n; ++px) {
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
                    s += 3; d += 4;
                }
            }
            if (!renderer.UploadVideoFrame(frame)) return Fail("frame upload failed");
        }

        const float t = startTime + static_cast<float>(f) / fps;

        if (animated) {
            for (auto& p : active->params) {
                if (!p.timeline || !p.timeline->enabled) continue;
                p.timeline->Evaluate(t, p.values, ValueCount(p.type));
            }
        }
        if (animated || f == 0) {
            float packed[kCustomFloats] = {};
            ShaderManager::PackParamValues(*active, packed);
            renderer.SetCustomUniforms(packed, kCustomFloats);
        }

        renderer.SetShaderTime(t);
        renderer.BeginFrame();
        if (!renderer.RenderToTexture()) return Fail("render failed");

        int gotW = 0, gotH = 0;
        if (!renderer.CopyRenderTargetToStaging(staged, gotW, gotH))
            return Fail("readback failed");
        if (gotW != width || gotH != height)
            return Fail("readback geometry " + std::to_string(gotW) + "x" + std::to_string(gotH) +
                        " does not match --size");

        if (channels == 4) {
            std::memcpy(outBuf.data(), staged.data(), inBytes);
        } else {
            const uint8_t* s = staged.data();
            uint8_t* d = outBuf.data();
            for (size_t px = 0, n = static_cast<size_t>(width) * height; px < n; ++px) {
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
                s += 4; d += 3;
            }
        }

        if (outFile.is_open()) outFile.write(reinterpret_cast<const char*>(outBuf.data()), inBytes);
        else if (std::fwrite(outBuf.data(), 1, inBytes, stdout) != inBytes)
            return Fail("short write to stdout (the reader closed the pipe)");

        ++done;
        if (verbose && (done % 30 == 0))
            std::cerr << "shaderfx: " << done << " frames\r";
    }

    if (outFile.is_open()) outFile.close(); else std::fflush(stdout);
    if (verbose) std::cerr << "shaderfx: " << done << " frames, " << active->name << "        \n";

    // A caller that asked for N frames and got fewer has a truncated input, which is
    // silent corruption in a render pipeline unless it is an error here.
    if (frameCount > 0 && done < frameCount)
        return Fail("input ended after " + std::to_string(done) + " of " +
                    std::to_string(frameCount) + " frames");

    renderer.Shutdown();
    return 0;
}
