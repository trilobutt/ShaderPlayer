#include "Common.h"
#include <algorithm>
#include <cmath>

namespace SP {

// Cubic bezier evaluation: given control points (0,0), (cx1,cy1), (cx2,cy2), (1,1),
// solve for Y at a given X using binary search on the parametric t.
static float EvalCubicBezier(float cx1, float cy1, float cx2, float cy2, float x) {
    // Binary search for t where bezierX(t) ≈ x
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 16; ++i) {
        float t = (lo + hi) * 0.5f;
        float omt = 1.0f - t;
        float bx = 3.0f * omt * omt * t * cx1
                  + 3.0f * omt * t * t * cx2
                  + t * t * t;
        if (bx < x) lo = t;
        else         hi = t;
    }
    float t = (lo + hi) * 0.5f;
    float omt = 1.0f - t;
    return 3.0f * omt * omt * t * cy1
         + 3.0f * omt * t * t * cy2
         + t * t * t;
}

static float Smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

bool KeyframeTimeline::Evaluate(float time, float out[4], int valueCount) const {
    if (!enabled || keyframes.empty()) return false;

    // Clamp to first/last keyframe
    if (time <= keyframes.front().time) {
        for (int i = 0; i < valueCount; ++i) out[i] = keyframes.front().values[i];
        return true;
    }
    if (time >= keyframes.back().time) {
        for (int i = 0; i < valueCount; ++i) out[i] = keyframes.back().values[i];
        return true;
    }

    // Binary search for segment
    int lo = 0, hi = static_cast<int>(keyframes.size()) - 1;
    while (lo < hi - 1) {
        int mid = (lo + hi) / 2;
        if (keyframes[mid].time <= time) lo = mid;
        else                              hi = mid;
    }

    const Keyframe& a = keyframes[lo];
    const Keyframe& b = keyframes[lo + 1];
    float segLen = b.time - a.time;
    float t = (segLen > 1e-9f) ? (time - a.time) / segLen : 0.0f;

    // Remap t based on interpolation mode
    float remapped = t;
    switch (a.mode) {
    case InterpolationMode::Linear:
        break;
    case InterpolationMode::EaseInOut:
        remapped = Smoothstep(t);
        break;
    case InterpolationMode::CubicBezier:
        remapped = EvalCubicBezier(a.handles.outX, a.handles.outY,
                                    a.handles.inX,  a.handles.inY, t);
        break;
    }

    // Lerp values
    for (int i = 0; i < valueCount; ++i) {
        out[i] = a.values[i] + (b.values[i] - a.values[i]) * remapped;
    }

    return true;
}

int KeyframeTimeline::AddKeyframe(const Keyframe& kf) {
    auto it = std::lower_bound(keyframes.begin(), keyframes.end(), kf.time,
        [](const Keyframe& k, float t) { return k.time < t; });
    int idx = static_cast<int>(it - keyframes.begin());
    keyframes.insert(it, kf);
    return idx;
}

void KeyframeTimeline::RemoveKeyframe(int index) {
    if (index >= 0 && index < static_cast<int>(keyframes.size())) {
        keyframes.erase(keyframes.begin() + index);
    }
}

} // namespace SP
