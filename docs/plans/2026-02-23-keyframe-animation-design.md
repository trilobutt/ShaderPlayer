# Keyframe Animation for Shader Parameters

**Date**: 2026-02-23
**Status**: Approved

## Overview

Add per-parameter keyframe animation to shader parameters, tied to absolute video time. Users enable keyframing per parameter via an advanced toggle, set keyframes at specific video timestamps, choose interpolation mode (linear, ease-in-out, or custom cubic bezier), and edit bezier curves via draggable control points.

## Data Model

### New Types (Common.h)

```cpp
enum class InterpolationMode { Linear, EaseInOut, CubicBezier };

struct BezierHandles {
    float outX = 0.33f, outY = 0.0f;  // control point leaving this keyframe
    float inX  = 0.67f, inY  = 1.0f;  // control point entering next keyframe
};

struct Keyframe {
    float time = 0.0f;
    float values[4] = {};
    InterpolationMode mode = InterpolationMode::Linear;
    BezierHandles handles;
};

struct KeyframeTimeline {
    bool enabled = false;
    std::vector<Keyframe> keyframes; // sorted by time

    bool Evaluate(float time, float out[4], int valueCount) const;
    void AddKeyframe(const Keyframe& kf);
    void RemoveKeyframe(int index);
};
```

### ShaderParam Extension

```cpp
struct ShaderParam {
    // ... existing fields ...
    std::optional<KeyframeTimeline> timeline; // nullopt until enabled
};
```

## Keyframeable Types

| Type | Interpolation | Value Count |
|------|--------------|-------------|
| Float | Continuous (lerp) | 1 |
| Color | Continuous (lerp per channel) | 4 |
| Point2D | Continuous (lerp per axis) | 2 |
| Bool | Step (snap to nearest keyframe) | 1 |
| Long | Step (snap to nearest keyframe) | 1 |
| Event | Not keyframeable | N/A |

## Evaluation Pipeline

```
ProcessFrame()          — decodes video, updates m_playbackTime
  ↓
EvaluateKeyframes()     — iterates active preset params, interpolates values
  ↓
OnParamChanged()        — packs into float[16], uploads to renderer
  ↓
BeginFrame()            — GPU gets updated constants
```

### KeyframeTimeline::Evaluate

1. Empty/disabled: return false.
2. Before first keyframe: clamp to first keyframe values.
3. After last keyframe: clamp to last keyframe values.
4. Binary search for segment where `kf[i].time <= time < kf[i+1].time`.
5. Normalise `t` within segment.
6. Apply interpolation based on `kf[i].mode`:
   - **Linear**: `lerp(a, b, t)`
   - **EaseInOut**: `lerp(a, b, smoothstep(t))`
   - **CubicBezier**: Remap `t` through cubic bezier curve (solve X→Y via Newton-Raphson), then `lerp(a, b, remapped_t)`
7. Bool/Long: step function, snap to `kf[i].values`.

## UI Design

### Per-Parameter Keyframe Toggle

- Small toggle button right of each param widget (hidden for Event type)
- Enabling constructs the `std::optional<KeyframeTimeline>`
- When enabled with keyframes and playing: widget becomes read-only, shows interpolated value
- "Add Keyframe" button captures current `m_playbackTime` + `param.values`
- Compact row of keyframe timestamp chips below widget

### Keyframe Detail Panel

When a keyframe is selected:
- Editable time field
- Value widget matching param type
- Interpolation mode combo (Linear, Ease In/Out, Custom Bezier)
- Inline bezier curve preview (~120x80px)
  - CubicBezier: draggable control point handles
  - Linear/EaseInOut: drawn but non-interactive (clicking EaseInOut converts to CubicBezier with equivalent handles)
- Delete keyframe button

### Transport Bar Markers

- Diamond markers at keyframe times overlaid on the timeline slider
- Shows markers for the currently-selected parameter only
- Right-click on transport while keyframing active: "Add keyframe here"

### Bezier Curve Editor

- Draws curve from (0,0) to (1,1) for interpolation between two keyframes
- Control points: X clamped to [0,1], Y unclamped (allows overshoot for bounce)
- Two handles with connecting lines to endpoints

## Persistence (config.json)

Extends existing per-preset serialisation:

```json
{
    "name": "pixelate",
    "filepath": "...",
    "paramValues": { "PixelSize": [8.0] },
    "keyframes": {
        "PixelSize": {
            "enabled": true,
            "keys": [
                {
                    "time": 0.0,
                    "values": [4.0],
                    "mode": "linear",
                    "handles": { "outX": 0.33, "outY": 0.0, "inX": 0.67, "inY": 1.0 }
                },
                {
                    "time": 5.0,
                    "values": [32.0],
                    "mode": "cubicBezier",
                    "handles": { "outX": 0.25, "outY": 0.1, "inX": 0.75, "inY": 0.9 }
                }
            ]
        }
    }
}
```

- `keyframes` object optional; absent = no keyframes
- Keyed by param name (survives recompiles)
- Mode as string: `"linear"`, `"easeInOut"`, `"cubicBezier"`
- Handles always serialised
- Unmatched entries silently dropped on load

## File Changes

| File | Change |
|------|--------|
| `Common.h` | New types: InterpolationMode, BezierHandles, Keyframe, KeyframeTimeline. Add `std::optional<KeyframeTimeline>` to ShaderParam. |
| `KeyframeTimeline.cpp` (new) | Evaluate(), AddKeyframe(), RemoveKeyframe(), bezier math. |
| `Application.cpp` | EvaluateKeyframes() in render loop. |
| `UIManager.cpp` | Keyframe toggle, chips, detail panel, bezier curve editor, transport markers. |
| `ConfigManager.cpp` | Serialise/deserialise keyframes per preset. |
| `CMakeLists.txt` | Add KeyframeTimeline.cpp. |

No changes to D3D11Renderer, ShaderManager, VideoDecoder, VideoEncoder, or WorkspaceManager.
