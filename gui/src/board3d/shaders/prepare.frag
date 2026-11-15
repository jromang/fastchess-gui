#version 330 core
// The denoiser's input: the mean radiance divided by the first hit's albedo (so that the filter blurs light, never the
// grain of the wood), the variance of that mean, and the mean normal and distance of the first hit.

layout(location = 0) out vec4 outLight;     // demodulated radiance, variance
layout(location = 1) out vec4 outGeometry;  // normal, distance

uniform sampler2D uRadiance, uGuide, uGeometry;

const float GUIDE_SAMPLES = 64.0;
const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

void main() {
    ivec2 p  = ivec2(gl_FragCoord.xy);
    vec4 r   = texelFetch(uRadiance, p, 0);
    vec4 g   = texelFetch(uGuide, p, 0);
    vec4 geo = texelFetch(uGeometry, p, 0);
    float n  = max(r.a, 1.0);
    float gn = min(n, GUIDE_SAMPLES);

    vec3 mean   = r.rgb / n;
    vec3 albedo = max(g.rgb / gn, vec3(0.02));
    float al    = dot(albedo, LUMA);
    vec3 light  = mean / albedo;
    float l     = dot(mean, LUMA);
    float var   = max(g.a / n - l * l, 0.0) / n / (al * al);

    // A pixel's own variance means nothing over one or two samples (it is zero over one): the spread of the light
    // around it instead, until there are enough samples (SVGF does the same before its history builds up).
    if (n < 4.0) {
        ivec2 size = textureSize(uRadiance, 0);
        float m1 = 0.0, m2 = 0.0, w = 0.0;
        for (int y = -2; y <= 2; ++y)
            for (int x = -2; x <= 2; ++x) {
                ivec2 q   = clamp(p + ivec2(x, y), ivec2(0), size - 1);
                vec4 rq   = texelFetch(uRadiance, q, 0);
                float nq  = max(rq.a, 1.0);
                vec3 aq   = max(texelFetch(uGuide, q, 0).rgb / min(nq, GUIDE_SAMPLES), vec3(0.02));
                float lq  = dot(rq.rgb / nq / aq, LUMA);
                m1 += lq, m2 += lq * lq, w += 1.0;
            }
        m1 /= w, m2 /= w;
        var = mix(max(m2 - m1 * m1, 0.0) / n, var, (n - 1.0) / 3.0);
    }
    outLight = vec4(light, var);

    vec3 normal = geo.xyz / gn;
    float len   = length(normal);
    outGeometry = vec4(len > 1e-4 ? normal / len : vec3(0.0, 1.0, 0.0), geo.w / gn);
}
