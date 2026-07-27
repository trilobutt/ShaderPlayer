/*{
    "INPUTS": [
        {"NAME": "LineColor",   "LABEL": "Line Colour",    "TYPE": "color",
         "DEFAULT": [1.0, 1.0, 1.0, 1.0]},
        {"NAME": "CropColor",   "LABEL": "Crop Colour",    "TYPE": "color",
         "DEFAULT": [1.0, 0.85, 0.2, 1.0]},
        {"NAME": "Preset",      "LABEL": "Preset",         "TYPE": "long",
         "VALUES": [0,1,2,3,4,5,6,7],
         "LABELS": ["Custom",
                    "EBU R95 (broadcast)",
                    "SMPTE 4:3 legacy",
                    "Live / IMAG",
                    "Social 1:1",
                    "Social 4:5",
                    "Social 9:16",
                    "Social 9:16 + UI"],
         "DEFAULT": 0},
        {"NAME": "ActionArea",  "LABEL": "Action Safe %",  "TYPE": "float",
         "MIN": 0.5, "MAX": 1.0, "DEFAULT": 0.9},
        {"NAME": "TitleArea",   "LABEL": "Title Safe %",   "TYPE": "float",
         "MIN": 0.5, "MAX": 1.0, "DEFAULT": 0.8},
        {"NAME": "Opacity",     "LABEL": "Opacity",        "TYPE": "float",
         "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.7},
        {"NAME": "LineWidth",   "LABEL": "Line Width (px)","TYPE": "float",
         "MIN": 1.0, "MAX": 8.0, "DEFAULT": 2.0},
        {"NAME": "ShowCenter",  "LABEL": "Show Centre Mark", "TYPE": "bool",
         "DEFAULT": true}
    ]
}*/

// Broadcast / live / social safe area guides.
//
// Two guide families are drawn:
//   * Action-safe and title-safe rectangles, centred, as a fraction of the frame.
//   * An aspect crop box (Crop Colour) showing what survives a re-crop to another
//     aspect ratio — the guide that matters for social deliverables cut from a
//     16:9 master. Title safe is then measured inside that crop, not the full frame.
//
// Presets:
//   Custom              action/title come straight from the sliders, no crop box.
//   EBU R95             93% action / 90% title — current European broadcast practice.
//   SMPTE 4:3 legacy    90% / 80% — the traditional guide, safe for 4:3 downconversion.
//   Live / IMAG         85% / 75% — generous margins for screen edge masking and
//                       keystone correction on projection rigs.
//   Social 1:1          square crop, title safe 90% of the crop.
//   Social 4:5          portrait feed crop (Instagram), title safe 90% of the crop.
//   Social 9:16         full vertical crop (Stories / Reels / TikTok / Shorts).
//   Social 9:16 + UI    as above, plus the top 12% / bottom 20% bands that platform
//                       chrome (captions, action buttons, handle) covers.
//
// Lines are antialiased over their own screen-space footprint, so they hold up when
// the viewport is scaled rather than dropping below a pixel and disappearing.

Texture2D    videoTexture : register(t0);
SamplerState videoSampler : register(s0);
Texture2D    noiseTexture : register(t1);
SamplerState noiseSampler : register(s1);

cbuffer Constants : register(b0) {
    float  time;
    float  padding1;
    float2 resolution;
    float2 videoResolution;
    float2 padding2;
    float4 custom[8];
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

// Antialiased border of a centred rectangle covering `frac` of the frame.
// halfW is the half-line-width in pixels; px is the size of one pixel in UV units.
float RectEdge(float2 uv, float2 rectFrac, float halfW, float2 px) {
    float2 half2 = rectFrac * 0.5;
    float2 d     = abs(uv - 0.5);

    // Signed distance to the rectangle outline, in pixels along each axis.
    float2 dPx   = (d - half2) / px;
    // Distance to the outline is the distance to the nearest of the two edges the
    // pixel is closest to; for an axis-aligned box this is the max of the per-axis
    // signed distances when outside, and the least-negative one when inside.
    float  outline = max(dPx.x, dPx.y);
    float  ring    = abs(outline);

    return 1.0 - smoothstep(halfW - 0.75, halfW + 0.75, ring);
}

// Antialiased horizontal band edge across the full width (for platform UI zones).
float BandEdge(float uvY, float edgeY, float halfW, float pxY) {
    float dPx = abs(uvY - edgeY) / pxY;
    return 1.0 - smoothstep(halfW - 0.75, halfW + 0.75, dPx);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv  = input.uv;
    float4 col = videoTexture.Sample(videoSampler, uv);

    float2 px    = 1.0 / resolution;
    float  halfW = max(LineWidth, 1.0) * 0.5;

    float frameAR = resolution.x / max(resolution.y, 1.0);

    // Resolve the preset into an action fraction, a title fraction and an optional
    // aspect crop. cropAR <= 0 means "no crop box for this preset".
    float  actionF = ActionArea;
    float  titleF  = TitleArea;
    float  cropAR  = -1.0;
    bool   showUI  = false;

    if      (Preset == 1) { actionF = 0.93; titleF = 0.90; }
    else if (Preset == 2) { actionF = 0.90; titleF = 0.80; }
    else if (Preset == 3) { actionF = 0.85; titleF = 0.75; }
    else if (Preset == 4) { cropAR  = 1.0;      titleF = 0.90; }
    else if (Preset == 5) { cropAR  = 0.8;      titleF = 0.90; }
    else if (Preset == 6) { cropAR  = 0.5625;   titleF = 0.90; }
    else if (Preset == 7) { cropAR  = 0.5625;   titleF = 0.90; showUI = true; }

    // Crop box: the largest rectangle of the target aspect that fits the frame.
    float2 cropFrac = float2(1.0, 1.0);
    bool   hasCrop  = cropAR > 0.0;
    if (hasCrop) {
        cropFrac = (cropAR < frameAR) ? float2(cropAR / frameAR, 1.0)
                                      : float2(1.0, frameAR / cropAR);
    }

    // Action and title rectangles. With a crop active the crop box replaces the action
    // rectangle and title safe is measured inside it, which is what a social
    // deliverable is actually judged against.
    float2 titleFrac = cropFrac * titleF;

    float onAction = hasCrop ? 0.0 : RectEdge(uv, float2(actionF, actionF), halfW, px);
    float onTitle  = RectEdge(uv, titleFrac, halfW, px);
    float onCrop   = hasCrop ? RectEdge(uv, cropFrac, halfW, px) : 0.0;

    // Platform chrome bands for vertical social formats.
    float onUI = 0.0;
    if (showUI) {
        float topY = 0.5 - cropFrac.y * 0.5 + cropFrac.y * 0.12;
        float botY = 0.5 + cropFrac.y * 0.5 - cropFrac.y * 0.20;
        bool  inX  = abs(uv.x - 0.5) <= cropFrac.x * 0.5;
        if (inX) onUI = max(BandEdge(uv.y, topY, halfW, px.y),
                            BandEdge(uv.y, botY, halfW, px.y));
    }

    // Centre crosshair: 24 px arms, gap of 8 px around exact centre.
    float onCross = 0.0;
    if (ShowCenter) {
        float2 fromCentre = abs(uv - 0.5) * resolution;
        float  crossLen   = 24.0;
        float  gapHalf    = 8.0;
        float  hArm = (1.0 - smoothstep(halfW - 0.75, halfW + 0.75, fromCentre.y))
                    * step(gapHalf, fromCentre.x) * step(fromCentre.x, crossLen);
        float  vArm = (1.0 - smoothstep(halfW - 0.75, halfW + 0.75, fromCentre.x))
                    * step(gapHalf, fromCentre.y) * step(fromCentre.y, crossLen);
        onCross = max(hArm, vArm);
    }

    // Composite, brightest guide wins. Title safe is drawn dimmer than action safe.
    float3 guideCol = LineColor.rgb;
    float  guideAmt = max(onAction, onCross);

    if (onTitle * 0.65 > guideAmt) {
        guideCol = LineColor.rgb * 0.65;
        guideAmt = onTitle;
    }
    if (max(onCrop, onUI) > guideAmt) {
        guideCol = CropColor.rgb * (onUI > onCrop ? 0.7 : 1.0);
        guideAmt = max(onCrop, onUI);
    }

    col.rgb = lerp(col.rgb, guideCol, saturate(guideAmt) * Opacity);
    return col;
}
