/*{
    "INPUTS": [
        {"NAME": "ActionArea",  "LABEL": "Action Safe %",  "TYPE": "float",
         "MIN": 0.5, "MAX": 1.0, "DEFAULT": 0.9},
        {"NAME": "TitleArea",   "LABEL": "Title Safe %",   "TYPE": "float",
         "MIN": 0.5, "MAX": 1.0, "DEFAULT": 0.8},
        {"NAME": "Opacity",     "LABEL": "Opacity",        "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.7},
        {"NAME": "ShowCenter",  "LABEL": "Show Centre Mark", "TYPE": "bool",
         "DEFAULT": true},
        {"NAME": "LineColor",   "LABEL": "Line Colour",    "TYPE": "color",
         "DEFAULT": [1.0, 1.0, 1.0, 1.0]}
    ]
}*/

// Broadcast Safe Areas
// Overlays action-safe and title-safe rectangle guides (both centred in frame).
//   ActionArea: outer rectangle, default 90% of frame — action should stay inside.
//   TitleArea:  inner rectangle, default 80% of frame — graphics/titles must stay inside.
//   ShowCenter: crosshair at frame centre.
// Aspect-ratio-aware: boxes are centred and proportional to the source frame.
// LineColor starts at cbuffer offset 4 (4-aligned after four floats).

Texture2D    videoTexture : register(t0);
SamplerState videoSampler : register(s0);

cbuffer Constants : register(b0) {
    float  time;
    float  padding1;
    float2 resolution;
    float2 videoResolution;
    float2 padding2;
    float4 custom[4];
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

// Returns true if uv is on the border of a centred rectangle covering `frac` of the frame.
// lineHalf is the half-line-width in normalised UV units.
bool OnRect(float2 uv, float rectFrac, float2 lineHalf) {
    float2 inner = (1.0 - rectFrac) * 0.5;          // margin from each edge
    float2 outer = inner + lineHalf * 2.0;           // outer extent of the line
    float2 d     = abs(uv - 0.5);                    // distance from centre

    bool inOuter = all(d <= float2(rectFrac * 0.5 + lineHalf.x, rectFrac * 0.5 + lineHalf.y));
    bool inInner = all(d <  float2(rectFrac * 0.5 - lineHalf.x, rectFrac * 0.5 - lineHalf.y));
    return inOuter && !inInner;
}

float4 main(PS_INPUT input) : SV_TARGET {
    float4 col = videoTexture.Sample(videoSampler, input.uv);

    // Line width: ~1.5 pixels in each axis
    float2 lineHalf = float2(1.5 / resolution.x, 1.5 / resolution.y);

    bool onAction = OnRect(input.uv, ActionArea, lineHalf);
    bool onTitle  = OnRect(input.uv, TitleArea,  lineHalf);

    // Centre crosshair: 24 px long, 1.5 px wide, gap of 8 px around exact centre
    bool onCross = false;
    if (ShowCenter) {
        float2 fromCentre = abs(input.uv - 0.5) * resolution;
        float  crossLen   = 24.0;
        float  gapHalf    = 8.0;
        bool   hArm       = fromCentre.y < 1.5 && fromCentre.x > gapHalf && fromCentre.x < crossLen;
        bool   vArm       = fromCentre.x < 1.5 && fromCentre.y > gapHalf && fromCentre.y < crossLen;
        onCross = hArm || vArm;
    }

    if (onAction || onTitle || onCross) {
        // Title area uses a slightly dimmer line to distinguish it from action safe
        float dimmer = onTitle && !onAction ? 0.65 : 1.0;
        col.rgb = lerp(col.rgb, LineColor.rgb * dimmer, Opacity);
    }

    return col;
}
