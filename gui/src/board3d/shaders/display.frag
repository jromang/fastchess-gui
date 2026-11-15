#version 330 core
// The image shown: the mean of the samples, or while they are few the denoiser's light times the albedo, fading to
// the mean as samples come; exposed, tone mapped with AgX, dithered to 8 bits.

out vec4 outColor;

uniform sampler2D uRadiance, uGuide, uFiltered;
uniform int uDenoise;
uniform float uExposure;
uniform vec2 uScale;  // traced size over shown size: below 1 for a preview

// AgX (Troy Sobotka's display transform) as fitted by Benjamin Wrensch's "Minimal AgX implementation" (iolite blog),
// with a mild punchy look. Its license is to be confirmed before this ships; Khronos PBR Neutral (Apache 2.0) would do
// otherwise.
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

void main() {
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
    c      = agx(c * uExposure);
    // Triangular dither of one step: no banding in the dark gradients.
    float d = hash(gl_FragCoord.xy) + hash(gl_FragCoord.xy + 17.3) - 1.0;
    outColor = vec4(clamp(c + d / 255.0, 0.0, 1.0), 1.0);
}
