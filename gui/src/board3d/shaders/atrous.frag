#version 330 core
// One step of the edge-aware à-trous wavelet filter (Dammertz et al. 2010, with the variance guidance of SVGF,
// Schied et al. 2017): a 5x5 B3 spline kernel, its taps uStep pixels apart, each weighted by how alike its normal,
// distance and light are to the centre's. The variance goes through with the squares of the weights.

out vec4 outLight;

uniform sampler2D uIn;        // demodulated radiance, variance
uniform sampler2D uGeometry;  // normal, distance
uniform int uStep;

const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

void main() {
    ivec2 size = textureSize(uIn, 0);
    ivec2 p    = ivec2(gl_FragCoord.xy);
    vec4 c     = texelFetch(uIn, p, 0);
    vec4 g     = texelFetch(uGeometry, p, 0);

    // The variance a little blurred, steadier for the light's weight.
    float var = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            float w = (x == 0 ? 0.5 : 0.25) * (y == 0 ? 0.5 : 0.25);
            var += w * texelFetch(uIn, clamp(p + ivec2(x, y), ivec2(0), size - 1), 0).a;
        }
    float lp     = dot(c.rgb, LUMA);
    float sigmaL = 4.0 * sqrt(max(var, 0.0)) + 1e-5;

    const float k[3] = float[3](0.375, 0.25, 0.0625);
    vec3 sum         = vec3(0.0);
    float sumVar = 0.0, sumW = 0.0;
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            ivec2 q = p + ivec2(dx, dy) * uStep;
            if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, size))) continue;
            vec4 cq   = texelFetch(uIn, q, 0);
            vec4 gq   = texelFetch(uGeometry, q, 0);
            float wn  = pow(max(dot(g.xyz, gq.xyz), 0.0), 64.0);
            float far = 0.004 * g.w * float(uStep) * length(vec2(dx, dy)) + 1e-3;
            float wz  = exp(-abs(g.w - gq.w) / far);
            float wl  = exp(-abs(lp - dot(cq.rgb, LUMA)) / sigmaL);
            float w   = k[abs(dx)] * k[abs(dy)] * wn * wz * wl;
            sum += w * cq.rgb;
            sumVar += w * w * cq.a;
            sumW += w;
        }
    outLight = sumW > 0.0 ? vec4(sum / sumW, sumVar / (sumW * sumW)) : c;
}
