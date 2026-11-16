#version 330 core
// The image shown: the mean of the samples, or while they are few the denoiser's light times the albedo, fading to
// the mean as samples come; exposed, tone mapped with AgX, dithered to 8 bits. Then what is laid over the board: tints
// over squares, dots, rings, arrows, drawn on its surface where the camera sees it, faintly through the pieces.

out vec4 outColor;

uniform sampler2D uRadiance, uGuide, uFiltered, uGeometry;
uniform int uDenoise;
uniform float uExposure;
uniform vec2 uScale;    // traced size over shown size: below 1 for a preview
uniform vec2 uOutSize;  // the size shown

// The overlay (renderer.hpp's Overlay), its colours sRGB with their opacity.
uniform vec3 uCamPos, uCamRight, uCamUp, uCamForward;
uniform float uTanHalfFov;
uniform vec4 uTint[64];
uniform int uMark[64];  // 0 none, 1 a dot, 2 a ring, 3 a king in check
uniform vec4 uMarkColour, uCheckColour;
uniform int uArrowCount;
uniform vec4 uArrow[8];  // from x, z, to x, z
uniform vec4 uArrowColour[8];
uniform int uArrowDashed;  // a bit per arrow: its shaft in dashes

const float HALF = 4.0;  // half the playing surface (trace.frag)
const float THROUGH = 0.35;  // how much of an arrow, a dot or a ring shows through a piece

// AgX, Troy Sobotka's display transform (https://github.com/sobotka/AgX), as Benjamin Wrensch fitted it in his
// "Minimal AgX implementation" (https://iolite-engine.com/blog_posts/minimal_agx_implementation), with a milder
// punchy look than his. Its notice:
//
// MIT License
//
// Copyright (c) 2024 Missing Deadlines (Benjamin Wrensch)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// All values used to derive this implementation are sourced from Troy's initial AgX implementation/OCIO config file
// available here: https://github.com/sobotka/AgX
vec3 agxContrast(vec3 x) {
    vec3 x2 = x * x, x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 agx(vec3 v) {
    const mat3 inset = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051, 0.0784335999999992,
                            0.878468636469772, 0.0784336, 0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 outset = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438, -0.0980208811401368,
                             1.15190312990417, -0.0980434501171241, -0.0990297440797205, -0.0989611768448433,
                             1.15107367264116);
    const float minEv = -12.47393, maxEv = 4.026069;
    v = inset * v;
    v = clamp(log2(max(v, vec3(1e-10))), minEv, maxEv);
    v = (v - minEv) / (maxEv - minEv);
    v = agxContrast(v);
    // Look: a little more contrast and colour.
    float luma = dot(v, vec3(0.2126, 0.7152, 0.0722));
    v          = pow(max(v, vec3(0.0)), vec3(1.2));
    v          = luma + 1.15 * (v - luma);
    return outset * v;  // display encoded already
}

float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

// The radiance of a traced pixel: the mean, or the denoised light times the albedo while samples are few.
vec3 radiance(ivec2 p) {
    p       = clamp(p, ivec2(0), textureSize(uRadiance, 0) - 1);
    vec4 a  = texelFetch(uRadiance, p, 0);
    float n = max(a.a, 1.0);
    vec3 c  = a.rgb / n;
    if (uDenoise == 1) {
        vec3 albedo = max(texelFetch(uGuide, p, 0).rgb / min(n, 64.0), vec3(0.02));
        vec3 clean  = texelFetch(uFiltered, p, 0).rgb * albedo;
        c           = mix(clean, c, smoothstep(48.0, 1024.0, n));
    }
    return c;
}

// The playing surface under this pixel: where the camera's ray through its centre meets it, t away, and the size of
// the pixel there (w, in squares).
struct Surface {
    vec2 q;  // in squares, x and z
    float t, w;
    bool board;  // on the 64 squares
};

Surface surface() {
    vec2 ndc     = gl_FragCoord.xy / uOutSize * 2.0 - 1.0;
    float aspect = uOutSize.x / uOutSize.y;
    vec3 dir     = normalize(uCamForward + uCamRight * ndc.x * uTanHalfFov * aspect + uCamUp * ndc.y * uTanHalfFov);
    Surface s;
    s.t     = dir.y < -1e-5 ? -uCamPos.y / dir.y : 1e5;
    s.q     = (uCamPos + dir * s.t).xz;
    s.w     = max(length(fwidth(s.q)), 1e-4);  // in every pixel: derivatives need their neighbours
    s.board = s.t < 1e5 && abs(s.q.x) < HALF && abs(s.q.y) < HALF;
    return s;
}

// The traced pixel under this one.
ivec2 traced() { return clamp(ivec2(gl_FragCoord.xy * uScale), ivec2(0), textureSize(uGeometry, 0) - 1); }
// The samples its guides were made of (trace.frag's GUIDE_SAMPLES at most).
float guideSamples(ivec2 p) { return min(max(texelFetch(uRadiance, p, 0).a, 1.0), 64.0); }

// How much of the playing surface the first hits of this pixel saw: 1 where nothing stands in front of it, 0 behind a
// piece.
float surfaceSeen(float t) {
    ivec2 p = traced();
    return smoothstep(0.985, 0.997, texelFetch(uGeometry, p, 0).w / guideSamples(p) / t);
}

// The part of a pixel w wide inside a shape whose signed distance is d.
float cover(float d, float w) { return clamp(0.5 - d / w, 0.0, 1.0); }

vec3 over(vec3 c, vec4 layer, float k) { return mix(c, layer.rgb, layer.a * k); }

int squareOf(vec2 q) { return clamp(int(floor(HALF - q.y)), 0, 7) * 8 + clamp(int(floor(q.x + HALF)), 0, 7); }
vec2 fromCentre(vec2 q) { return q - (floor(q + vec2(HALF, -HALF)) + vec2(-3.5, 4.5)); }

// The tints, in the light of the scene (before the tone mapping): a glaze on the square, its colour lit as the square
// is, shadows and reflections kept.
vec3 tinted(vec3 c, Surface s) {
    vec4 tint = uTint[squareOf(s.q)];
    if (tint.a <= 0.0) return c;
    vec2 l        = fromCentre(s.q);
    ivec2 p       = traced();
    vec3 albedo   = max(texelFetch(uGuide, p, 0).rgb / guideSamples(p), vec3(0.02));
    vec3 lit      = pow(tint.rgb, vec3(2.2)) * c / albedo;
    float k       = tint.a * surfaceSeen(s.t) * cover(max(abs(l.x), abs(l.y)) - 0.5, s.w);
    return mix(c, lit, k);
}

// The marks and arrows, flat as on the 2D board (after the tone mapping), faintly through the pieces.
vec3 marked(vec3 c, Surface s) {
    vec2 l        = fromCentre(s.q);
    float through = mix(THROUGH, 1.0, surfaceSeen(s.t));
    int mark      = uMark[squareOf(s.q)];
    if (mark == 1) c = over(c, uMarkColour, through * cover(length(l) - 0.15, s.w));
    if (mark == 2) c = over(c, uMarkColour, through * cover(abs(length(l) - 0.45) - 0.035, s.w));
    if (mark == 3) c = over(c, uCheckColour, through * cover(length(l) - 0.48, s.w));

    // The arrows as the 2D board draws them: a shaft from the first square's centre, a head stopping short of the
    // second's.
    const float shaft = 0.09, head = 0.24, headLength = 0.42, stop = 0.12;
    for (int k = 0; k < uArrowCount; ++k) {
        vec2 a = uArrow[k].xy, b = uArrow[k].zw;
        float len = length(b - a);
        if (len < 0.01) continue;
        vec2 u       = (b - a) / len;
        vec2 r       = s.q - a;
        float along  = dot(r, u), across = abs(dot(r, vec2(-u.y, u.x)));
        float tip    = len - stop, base = tip - headLength, slope = head / headLength;
        float dShaft = max(across - shaft, max(-along, along - base));
        // Dashed, as the 2D board: dashes of 0.22 square and gaps of 0.14 from the first square's centre.
        if (((uArrowDashed >> k) & 1) != 0) {
            float phase = mod(along, 0.36);
            float dDash = phase < 0.22 ? max(-phase, phase - 0.22) : min(phase - 0.22, 0.36 - phase);
            dShaft      = max(dShaft, dDash);
        }
        float dHead  = max((across - slope * (tip - along)) / sqrt(1.0 + slope * slope), base - along);
        c = over(c, uArrowColour[k], through * cover(min(dShaft, dHead), s.w));
    }
    return c;
}

void main() {
    Surface s = surface();
    vec3 c;
    if (uScale.x >= 0.999 && uScale.y >= 0.999) {
        c = radiance(ivec2(gl_FragCoord.xy));
    } else {  // a preview, stretched: bilinear between the four nearest traced pixels
        vec2 q  = gl_FragCoord.xy * uScale - 0.5;
        ivec2 i = ivec2(floor(q));
        vec2 f  = q - floor(q);
        c = mix(mix(radiance(i), radiance(i + ivec2(1, 0)), f.x),
                mix(radiance(i + ivec2(0, 1)), radiance(i + ivec2(1, 1)), f.x), f.y);
    }
    if (s.board) c = tinted(c, s);
    c = agx(c * uExposure);
    if (s.board) c = marked(c, s);
    // Triangular dither of one step: no banding in the dark gradients.
    float d = hash(gl_FragCoord.xy) + hash(gl_FragCoord.xy + 17.3) - 1.0;
    outColor = vec4(clamp(c + d / 255.0, 0.0, 1.0), 1.0);
}
