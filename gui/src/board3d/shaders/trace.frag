#version 330 core
// One path-traced sample of each pixel of the 3D board, added to the accumulation buffer (alpha counts the samples).
//
// Scene, in squares (1 = one square), y up, White at +z: the playing surface is y = 0 over |x|, |z| < 4 inside a
// frame; the board is a box down to the floor of a photographer's studio, a seamless sweep whose glowing ceiling is
// the ambient light; up to four rectangular lights; the pieces are instances of six meshes, each a bounding volume
// hierarchy in texture buffers (bvh.hpp).

layout(location = 0) out vec4 outColor;  // radiance, 1
layout(location = 1) out vec4 outGuide;  // first hit's albedo (the first GUIDE_SAMPLES only), luminance squared
layout(location = 2) out vec4 outGeom;   // first hit's normal and distance (the first GUIDE_SAMPLES only)

uniform samplerBuffer uNodes;  // 4 texels per node
uniform samplerBuffer uTris;   // v0, e1, e2 per triangle
uniform samplerBuffer uNorms;  // n0, n1, n2 per triangle
// Each piece: (x, z, cos, sin) of its turn round y, (kind, black, scale, root node), its box.
#define MAX_INST 40
uniform vec4 uInstXf[MAX_INST], uInstInfo[MAX_INST], uInstMin[MAX_INST], uInstMax[MAX_INST];
uniform int uInstCount;
uniform int uSquare[64];  // the piece standing on each square (0 = a1 ... 63 = h8), -1 none
uniform float uPieceTop;  // the top of the tallest piece

uniform vec2 uResolution;
uniform int uSample;
uniform int uBounces;

uniform vec3 uCamPos, uCamRight, uCamUp, uCamForward;
uniform float uTanHalfFov, uAperture, uFocusDist;

#define MAX_LIGHTS 4
uniform int uLightCount;
uniform vec3 uLightC[MAX_LIGHTS], uLightU[MAX_LIGHTS], uLightV[MAX_LIGHTS], uLightL[MAX_LIGHTS];
uniform float uLightCdf[MAX_LIGHTS];  // lights picked by their power
uniform float uLightSun[MAX_LIGHTS];  // 0 for a rectangle; for a sun, far away, the cosine of its angular radius,
                                      // uLightC its direction
uniform vec3 uSky;                    // the radiance of the studio's ceiling
uniform vec3 uBackdrop;               // its wall's and far floor's, rising from nothing round the board
uniform vec4 uFloor;                  // the studio's albedo, linear, and how much its weave shows
uniform vec3 uFloorSurface;           // its roughness, clear coat and the coat's roughness
uniform vec2 uVarnish;                // the board's clear coat and its roughness

const float PI       = 3.14159265358979;
const float INF      = 1e30;
const float EPS      = 2e-4;
const float HALF     = 4.0;    // half the playing surface
const float FRAME    = 0.62;   // width of the frame round it
const float THICK    = 0.32;   // the board's thickness: the table is at y = -THICK
const float STRING   = 0.035;  // the light inlay between the squares and the frame

// The studio: the floor curves up into a round wall, and the wall into the ceiling, with no edge anywhere, so no
// horizon shows and the light fades out on it as it does on a photographer's paper sweep.
const float ROOM_R    = 36.0;  // the wall's radius
const float ROOM_COVE = 12.0;  // the radius of the curves, floor to wall and wall to ceiling
const float ROOM_H    = 30.0;  // floor to ceiling

const int MAT_ROOM = 1, MAT_BOARD = 2, MAT_PIECE = 3, MAT_LIGHT = 4;
const int GUIDE_SAMPLES = 64;  // the denoiser's guides: enough samples to antialias them, then no more

// What the first hit of the path was, for the denoiser.
vec3 gAlbedo = vec3(1.0), gNormal = vec3(0.0, 1.0, 0.0);
float gDepth = 1000.0;

// ---------------------------------------------------------------------------------------------------------------
// Random numbers

uint rngState;
uint pcg(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
float rnd() {
    rngState = pcg(rngState);
    return float(rngState >> 8) * (1.0 / 16777216.0);
}

float hash1(float n) { return fract(sin(n * 12.9898) * 43758.5453); }
float hash3(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float noise(vec3 x) {
    vec3 i = floor(x), f = fract(x);
    f      = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash3(i + vec3(0, 0, 0)), hash3(i + vec3(1, 0, 0)), f.x),
                   mix(hash3(i + vec3(0, 1, 0)), hash3(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(hash3(i + vec3(0, 0, 1)), hash3(i + vec3(1, 0, 1)), f.x),
                   mix(hash3(i + vec3(0, 1, 1)), hash3(i + vec3(1, 1, 1)), f.x), f.y),
               f.z);
}
float fbm(vec3 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; ++i) {
        s += a * noise(p);
        p = p * 2.03 + 17.1;
        a *= 0.5;
    }
    return s;
}

vec3 srgb(vec3 c) { return pow(c / 255.0, vec3(2.2)); }

// ---------------------------------------------------------------------------------------------------------------
// Intersections

float slab(vec3 ro, vec3 inv, vec3 lo, vec3 hi, float tmax) {
    vec3 t0 = (lo - ro) * inv, t1 = (hi - ro) * inv;
    vec3 tn = min(t0, t1), tf = max(t0, t1);
    float a = max(max(tn.x, tn.y), max(tn.z, 0.0));
    float b = min(min(tf.x, tf.y), min(tf.z, tmax));
    return a <= b ? a : INF;
}

vec3 safeInverse(vec3 d) {
    return 1.0 / vec3(abs(d.x) > 1e-9 ? d.x : 1e-9, abs(d.y) > 1e-9 ? d.y : 1e-9, abs(d.z) > 1e-9 ? d.z : 1e-9);
}

bool triangles(int first, int count, vec3 ro, vec3 rd, inout float tBest, inout int tri, inout vec2 bary,
               bool any) {
    bool hit = false;
    for (int k = 0; k < count; ++k) {
        int i   = first + k;
        vec3 v0 = texelFetch(uTris, 3 * i).xyz;
        vec3 e1 = texelFetch(uTris, 3 * i + 1).xyz;
        vec3 e2 = texelFetch(uTris, 3 * i + 2).xyz;
        vec3 p  = cross(rd, e2);
        float det = dot(e1, p);
        if (abs(det) < 1e-12) continue;
        float id = 1.0 / det;
        vec3 s   = ro - v0;
        float u  = dot(s, p) * id;
        if (u < 0.0 || u > 1.0) continue;
        vec3 q  = cross(s, e1);
        float v = dot(rd, q) * id;
        if (v < 0.0 || u + v > 1.0) continue;
        float t = dot(e2, q) * id;
        if (t > 0.0 && t < tBest) {
            tBest = t, tri = i, bary = vec2(u, v), hit = true;
            if (any) return true;
        }
    }
    return hit;
}

// The closest triangle of a mesh, from its root node, nearer than tBest (or any, for shadows).
bool traverse(int root, vec3 ro, vec3 rd, inout float tBest, inout int tri, inout vec2 bary, bool any) {
    vec3 inv = safeInverse(rd);
    int stack[16];
    int sp   = 0;
    int node = root;
    bool hit = false;
    for (int guard = 0; guard < 4096; ++guard) {
        vec4 a = texelFetch(uNodes, 4 * node), b = texelFetch(uNodes, 4 * node + 1);
        vec4 c = texelFetch(uNodes, 4 * node + 2), d = texelFetch(uNodes, 4 * node + 3);
        float tl = slab(ro, inv, a.xyz, b.xyz, tBest);
        float tr = slab(ro, inv, c.xyz, d.xyz, tBest);
        int lref = floatBitsToInt(a.w), lcount = floatBitsToInt(b.w);
        int rref = floatBitsToInt(c.w), rcount = floatBitsToInt(d.w);
        if (tl < INF && lcount > 0) {
            if (triangles(lref, lcount, ro, rd, tBest, tri, bary, any)) {
                hit = true;
                if (any) return true;
            }
            tl = INF;
        }
        if (tr < INF && rcount > 0) {
            if (triangles(rref, rcount, ro, rd, tBest, tri, bary, any)) {
                hit = true;
                if (any) return true;
            }
            tr = INF;
        }
        bool goL = tl < tBest, goR = tr < tBest;
        if (goL && goR) {
            bool leftFirst = tl <= tr;
            if (sp < 16) stack[sp++] = leftFirst ? rref : lref;
            node = leftFirst ? lref : rref;
        } else if (goL) {
            node = lref;
        } else if (goR) {
            node = rref;
        } else {
            if (sp == 0) break;
            node = stack[--sp];
        }
    }
    return hit;
}

// The pieces a ray meets over the board, square by square from the nearest (Amanatides and Woo's walk through a grid):
// a piece stands inside its square, so the first one hit is the nearest, and a ray that never passes below the
// tallest piece's top over the board meets none. The piece hit, -1 none; with `any`, the first hit found.
int piecesHit(vec3 ro, vec3 rd, inout float tBest, inout int tri, inout vec2 bary, out vec3 hitLo, out vec3 hitLd,
              bool any) {
    hitLo = hitLd = vec3(0.0);
    vec3 inv    = safeInverse(rd);
    vec3 t0     = (vec3(-HALF, 0.0, -HALF) - ro) * inv, t1 = (vec3(HALF, uPieceTop, HALF) - ro) * inv;
    vec3 tn     = min(t0, t1), tf = max(t0, t1);
    float enter = max(max(tn.x, tn.y), max(tn.z, 0.0));
    float leave = min(min(tf.x, tf.y), min(tf.z, tBest));
    if (enter > leave) return -1;
    ivec2 cell = clamp(ivec2(floor(ro.xz + rd.xz * enter + HALF)), ivec2(0), ivec2(7));
    ivec2 step = ivec2(rd.x >= 0.0 ? 1 : -1, rd.z >= 0.0 ? 1 : -1);
    vec2 next  = (vec2(cell) - HALF + vec2(step.x > 0 ? 1.0 : 0.0, step.y > 0 ? 1.0 : 0.0) - ro.xz) * inv.xz;
    vec2 delta = abs(inv.xz);
    for (int k = 0; k < 16; ++k) {
        int i = uSquare[(7 - cell.y) * 8 + cell.x];  // z grows towards White: the 8th rank at z = -4
        if (i >= 0 && slab(ro, inv, uInstMin[i].xyz, uInstMax[i].xyz, tBest) < INF) {
            vec4 a = uInstXf[i], b = uInstInfo[i];
            vec3 p  = ro - vec3(a.x, 0.0, a.y);
            vec3 lo = vec3(a.z * p.x + a.w * p.z, p.y, -a.w * p.x + a.z * p.z) / b.z;
            vec3 ld = vec3(a.z * rd.x + a.w * rd.z, rd.y, -a.w * rd.x + a.z * rd.z) / b.z;
            if (traverse(int(b.w), lo, ld, tBest, tri, bary, any)) {
                hitLo = lo, hitLd = ld;
                return i;
            }
        }
        if (min(next.x, next.y) >= leave) break;
        if (next.x < next.y) cell.x += step.x, next.x += delta.x;
        else cell.y += step.y, next.y += delta.y;
        if (cell.x < 0 || cell.x > 7 || cell.y < 0 || cell.y > 7) break;
    }
    return -1;
}

struct Hit {
    float t;
    int mat;
    int id;      // the piece, or the light
    vec3 n;      // shading normal, world
    vec3 ng;     // geometric normal, world
    vec3 local;  // position in the piece's own space
};

// The board: a box from the table up to y = 0, of half width HALF + FRAME.
bool boardBox(vec3 ro, vec3 inv, float tmax, out float t, out vec3 n) {
    vec3 lo = vec3(-HALF - FRAME, -THICK, -HALF - FRAME), hi = vec3(HALF + FRAME, 0.0, HALF + FRAME);
    vec3 t0 = (lo - ro) * inv, t1 = (hi - ro) * inv;
    vec3 tn = min(t0, t1), tf = max(t0, t1);
    float a = max(max(tn.x, tn.y), tn.z);
    float b = min(min(tf.x, tf.y), tf.z);
    if (a > b || a < EPS || a >= tmax) return false;
    t = a;
    n = a == tn.x ? vec3(-sign(inv.x), 0, 0) : a == tn.y ? vec3(0, -sign(inv.y), 0) : vec3(0, 0, -sign(inv.z));
    return true;
}

// The studio, a surface of revolution round y: in the plane of r and y, a rectangle with rounded corners. Positive
// inside, the distance to its surface, exact (the nearest point lies in the same plane through the axis).
float roomDist(vec3 p) {
    vec2 q = vec2(length(p.xz), p.y - (-THICK + 0.5 * ROOM_H));
    vec2 d = abs(q) - vec2(ROOM_R - ROOM_COVE, 0.5 * ROOM_H - ROOM_COVE);
    return ROOM_COVE - length(max(d, 0.0)) - min(max(d.x, d.y), 0.0);
}

// Its normal at p, inwards.
vec3 roomNormal(vec3 p) {
    float r = length(p.xz);
    vec2 q  = vec2(r, p.y - (-THICK + 0.5 * ROOM_H));
    vec2 d  = abs(q) - vec2(ROOM_R - ROOM_COVE, 0.5 * ROOM_H - ROOM_COVE);
    vec2 g  = (d.x > 0.0 && d.y > 0.0) ? normalize(d) : (d.x > d.y ? vec2(1.0, 0.0) : vec2(0.0, 1.0));
    g.y *= sign(q.y);
    vec2 radial = r > 1e-6 ? p.xz / r : vec2(1.0, 0.0);
    return -vec3(radial.x * g.x, g.y, radial.y * g.x);
}

// Where a ray from inside leaves the studio. The studio being convex, a ray leaves it once: if that is on the flat
// floor or ceiling or the straight band of the wall, solved exactly; otherwise on a curve, beyond the flat floor's
// radius, marched from there.
bool roomHit(vec3 ro, vec3 rd, out float t, out vec3 n) {
    const float y0 = -THICK, y1 = -THICK + ROOM_H, flatR = ROOM_R - ROOM_COVE;
    t = INF;
    n = vec3(0.0, 1.0, 0.0);
    if (roomDist(ro) < -0.01) return false;  // a camera outside sees nothing of it
    if (abs(rd.y) > 1e-9) {
        float tp = ((rd.y < 0.0 ? y0 : y1) - ro.y) / rd.y;
        vec2 q   = ro.xz + rd.xz * tp;
        if (tp > EPS && dot(q, q) <= flatR * flatR) {
            t = tp;
            n = vec3(0.0, rd.y < 0.0 ? 1.0 : -1.0, 0.0);
            return true;
        }
    }
    float a = dot(rd.xz, rd.xz), b = dot(ro.xz, rd.xz), r2 = dot(ro.xz, ro.xz);
    float tm = 0.0;
    if (a > 1e-12) {
        float disc = b * b - a * (r2 - ROOM_R * ROOM_R);
        if (disc >= 0.0) {
            float tw = (-b + sqrt(disc)) / a;
            float y  = ro.y + rd.y * tw;
            if (tw > EPS && y >= y0 + ROOM_COVE && y <= y1 - ROOM_COVE) {
                vec2 q = ro.xz + rd.xz * tw;
                t      = tw;
                n      = vec3(-q.x, 0.0, -q.y) / ROOM_R;
                return true;
            }
        }
        if (r2 < flatR * flatR) tm = (-b + sqrt(b * b - a * (r2 - flatR * flatR))) / a;
    }
    for (int i = 0; i < 96; ++i) {
        float d = roomDist(ro + rd * tm);
        if (abs(d) < 1e-4 + 1e-4 * tm && tm > 1e-3) break;
        tm += max(d, 2e-4);
    }
    // A glancing exit, low on the curve, can stop the march short of it: the studio being closed, the ray leaves
    // it about there, and no pixel shows anything else.
    t = tm;
    n = roomNormal(ro + rd * tm);
    return true;
}

// Only the side that shines is there: from behind, or edge on, a light lets the ray through.
bool lightHit(int i, vec3 ro, vec3 rd, float tmax, out float t) {
    vec3 n = normalize(cross(uLightU[i], uLightV[i]));
    float dn = dot(rd, n);
    if (dn > -1e-6) return false;
    t = dot(uLightC[i] - ro, n) / dn;
    if (t < EPS || t >= tmax) return false;
    vec3 q = ro + rd * t - uLightC[i];
    float u = dot(q, uLightU[i]) / dot(uLightU[i], uLightU[i]);
    float v = dot(q, uLightV[i]) / dot(uLightV[i], uLightV[i]);
    return abs(u) <= 0.5 && abs(v) <= 0.5;
}

bool intersect(vec3 ro, vec3 rd, bool lights, out Hit h) {
    h.t   = INF;
    h.mat = 0;
    vec3 inv = safeInverse(rd);

    float tr;
    vec3 nr;
    if (roomHit(ro, rd, tr, nr)) h.t = tr, h.mat = MAT_ROOM, h.n = h.ng = nr;
    float tb;
    vec3 nb;
    if (boardBox(ro, inv, h.t, tb, nb)) h.t = tb, h.mat = MAT_BOARD, h.n = h.ng = nb;

    if (lights) {
        for (int i = 0; i < uLightCount; ++i) {
            float tl;
            if (uLightSun[i] == 0.0 && lightHit(i, ro, rd, h.t, tl)) h.t = tl, h.mat = MAT_LIGHT, h.id = i;
        }
    }

    int tri = 0;
    vec2 bary;
    vec3 hitLo, hitLd;
    int hitInst = piecesHit(ro, rd, h.t, tri, bary, hitLo, hitLd, false);
    if (hitInst >= 0) {
        vec4 a  = uInstXf[hitInst];
        vec3 n0 = texelFetch(uNorms, 3 * tri).xyz, n1 = texelFetch(uNorms, 3 * tri + 1).xyz;
        vec3 n2 = texelFetch(uNorms, 3 * tri + 2).xyz;
        vec3 n  = normalize(n0 * (1.0 - bary.x - bary.y) + n1 * bary.x + n2 * bary.y);
        vec3 ng = normalize(cross(texelFetch(uTris, 3 * tri + 1).xyz, texelFetch(uTris, 3 * tri + 2).xyz));
        // From the piece's space to the world: the turn round y.
        h.n     = vec3(a.z * n.x - a.w * n.z, n.y, a.w * n.x + a.z * n.z);
        h.ng    = vec3(a.z * ng.x - a.w * ng.z, ng.y, a.w * ng.x + a.z * ng.z);
        h.mat   = MAT_PIECE;
        h.id    = hitInst;
        h.local = hitLo + hitLd * h.t;
    }
    return h.mat != 0;
}

bool occluded(vec3 ro, vec3 rd, float tmax) {
    vec3 inv = safeInverse(rd);
    float tb;
    vec3 nb;
    if (boardBox(ro, inv, tmax, tb, nb)) return true;
    float t = tmax;
    int tri = 0;
    vec2 bary;
    vec3 lo, ld;
    return piecesHit(ro, rd, t, tri, bary, lo, ld, true) >= 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Materials

struct Material {
    vec3 albedo;
    float rough;    // base specular roughness (perceptual)
    float coat;     // clear coat weight
    float coatRough;
};

// Wood seen on a plane: `along` runs with the grain, `across` over it; rings of a log lying under the surface.
vec3 wood(float along, float across, float seed, vec3 early, vec3 late, float rings, float contrast, out float figure) {
    float depth = 1.2 + 3.0 * hash1(seed);
    float off   = (hash1(seed + 3.1) - 0.5) * 6.0;
    vec3 p      = vec3(along, across + off, depth);
    float warp  = fbm(vec3(p.x * 0.35, p.y * 1.6, p.z * 1.6 + seed)) - 0.5;
    float r     = length(p.yz) + 0.22 * warp + 0.015 * sin(p.x * 2.1 + seed * 5.0);
    float ring  = fract(r * rings);
    float late_ = smoothstep(0.62, 0.92, ring) * (1.0 - smoothstep(0.96, 1.0, ring));
    float fibre = noise(vec3(along * 3.0, across * 160.0, seed * 7.3));
    float fine  = noise(vec3(along * 1.1, across * 40.0, seed * 2.9 + 11.0));
    float t     = late_ * 0.85 * contrast + 0.3 * (fibre - 0.5) + 0.25 * (fine - 0.5) + 0.5 * (1.0 - contrast) * 0.3;
    vec3 c      = mix(early, late, clamp(t, 0.0, 1.0));
    figure      = noise(vec3(along * 0.8, across * 9.0, seed + 5.0));
    return c * (0.93 + 0.14 * figure);
}

Material boardMaterial(vec3 p, vec3 n) {
    Material m;
    m.coat      = uVarnish.x;
    m.coatRough = uVarnish.y;
    m.rough     = 0.55;
    float figure;
    float ax = abs(p.x), az = abs(p.z);
    if (n.y > 0.5 && ax < HALF && az < HALF) {
        // A square: maple or walnut, each its own piece of veneer, the grain running from one player to the other.
        float file = floor(p.x + HALF), rank = floor(HALF - p.z);
        bool dark  = mod(file + rank, 2.0) < 0.5;
        float seed = file * 8.0 + rank + 1.0;
        if (dark)  // walnut
            m.albedo = wood(p.z, p.x, seed, srgb(vec3(98, 66, 44)), srgb(vec3(52, 34, 22)), 6.0, 0.75, figure);
        else  // maple, pale and quiet
            m.albedo = wood(p.z, p.x, seed, srgb(vec3(226, 196, 148)), srgb(vec3(206, 170, 118)), 4.5, 0.45, figure);
        // The thin dark joint between squares.
        vec2 f = abs(fract(p.xz + HALF) - 0.5);
        float joint = smoothstep(0.4985, 0.4995, max(f.x, f.y));
        m.albedo *= 1.0 - 0.45 * joint;
        return m;
    }
    float edge = max(ax, az);
    if (n.y > 0.5 && edge < HALF + STRING) {
        m.albedo = srgb(vec3(226, 200, 150));  // boxwood stringing
        return m;
    }
    // The frame: walnut, mitred at the corners, its grain along each side.
    bool sideX = (n.y > 0.5) ? az > ax : abs(n.z) > 0.5;
    float along  = sideX ? p.x : p.z;
    float across = sideX ? p.z : p.x;
    if (n.y < 0.5) across = p.y * 1.0 + (sideX ? p.z : p.x);
    float seed = sideX ? (p.z > 0.0 ? 101.0 : 102.0) : (p.x > 0.0 ? 103.0 : 104.0);
    m.albedo   = wood(along, across, seed, srgb(vec3(84, 52, 32)), srgb(vec3(44, 26, 15)), 8.0, 0.9, figure);
    return m;
}

Material pieceMaterial(vec3 local, bool black) {
    Material m;
    float figure;
    // A turned piece: the grain runs up it.
    float a = atan(local.z, local.x);
    if (black) {  // ebony, polished
        m.albedo    = wood(local.y * 0.7, a * 0.35, 7.0, srgb(vec3(28, 22, 20)), srgb(vec3(16, 12, 11)), 11.0, 0.6, figure);
        m.rough     = 0.3;
        m.coat      = 1.0;
        m.coatRough = 0.05;
    } else {  // boxwood, satin: hardly any grain
        m.albedo    = wood(local.y * 0.7, a * 0.35, 3.0, srgb(vec3(248, 236, 212)), srgb(vec3(240, 224, 192)), 5.0, 0.2, figure);
        m.rough     = 0.4;
        m.coat      = 0.9;
        m.coatRough = 0.08;
    }
    // The baize under it.
    if (local.y < 0.004) {
        m.albedo = srgb(vec3(20, 60, 32));
        m.rough  = 1.0;
        m.coat   = 0.0;
    }
    return m;
}

// The studio's floor and walls, of the lighting's own colour; its ceiling, the glow of a large diffuser, fading into
// the wall along the upper curve. Only the flat floor has the lighting's finish, a sheet of acrylic for instance; the
// curves and the wall are matt, as a backdrop is. n inwards.
Material roomMaterial(vec3 p, vec3 n, out vec3 glow) {
    Material m;
    float ceiling = clamp(-n.y, 0.0, 1.0);
    ceiling *= ceiling;
    // The backdrop's glow: the wall, and the floor too away from the board, so that floor and wall are one white
    // with no line between them; none near the board, which keeps its shadows.
    float backdrop = max(1.0 - abs(n.y), n.y >= 0.0 ? smoothstep(7.0, 20.0, length(p.xz)) : 0.0);
    glow           = uSky * ceiling + uBackdrop * backdrop;
    float weave = noise(p * 60.0) * 0.5 + noise(p * 8.0 + 3.0) * 0.5;
    m.albedo    = uFloor.rgb * (1.0 + uFloor.w * (weave - 0.5)) * (1.0 - ceiling);
    float floor_ = smoothstep(0.97, 1.0, n.y);
    m.rough     = mix(1.0, uFloorSurface.x, floor_);
    m.coat      = uFloorSurface.y * floor_;
    m.coatRough = uFloorSurface.z;
    return m;
}

// A ray that left the studio, which only a camera outside it can see.
vec3 ambient(vec3 d) { return uSky * 0.5; }

// ---------------------------------------------------------------------------------------------------------------
// BSDF: a clear coat over a dielectric base (diffuse plus GGX specular), both coats with F0 = 0.04.

float fresnel(float c) { return 0.04 + 0.96 * pow(1.0 - c, 5.0); }
float ggx(float NoH, float a2) {
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}
float smith1(float NoX, float a2) { return 2.0 * NoX / (NoX + sqrt(a2 + (1.0 - a2) * NoX * NoX)); }
float alpha2(float rough) { return max(rough * rough * rough * rough, 1e-7); }

void lobes(Material m, float NoV, out float pd, out float ps, out float pc) {
    float fc  = m.coat * fresnel(NoV);
    float fs  = fresnel(NoV) * (1.0 - fc);
    float lum = dot(m.albedo, vec3(0.2126, 0.7152, 0.0722));
    float fd  = lum * (1.0 - fresnel(NoV)) * (1.0 - fc);
    float sum = fc + fs + fd + 1e-6;
    pd = fd / sum, ps = fs / sum, pc = fc / sum;
}

// f(v, l) * cos(l), and the pdf of l as sampleBsdf draws it.
vec3 evalBsdf(Material m, vec3 n, vec3 v, vec3 l, out float pdf) {
    pdf       = 0.0;
    float NoL = dot(n, l), NoV = max(dot(n, v), 1e-4);
    if (NoL <= 0.0) return vec3(0.0);
    vec3 h    = normalize(v + l);
    float NoH = max(dot(n, h), 0.0), VoH = max(dot(v, h), 0.0);
    float ab = alpha2(m.rough), ac = alpha2(m.coatRough);
    float F  = fresnel(VoH);
    float Db = ggx(NoH, ab), Dc = ggx(NoH, ac);
    float specB = Db * smith1(NoV, ab) * smith1(NoL, ab) * F / (4.0 * NoV * NoL);
    float specC = m.coat * Dc * smith1(NoV, ac) * smith1(NoL, ac) * F / (4.0 * NoV * NoL);
    float attenuation = (1.0 - m.coat * fresnel(NoV)) * (1.0 - m.coat * fresnel(NoL));
    vec3 diffuse      = m.albedo / PI * (1.0 - fresnel(NoL)) * (1.0 - fresnel(NoV)) * 1.05;
    vec3 f            = vec3(specC) + attenuation * (diffuse + vec3(specB));
    float pd, ps, pc;
    lobes(m, NoV, pd, ps, pc);
    pdf = pd * NoL / PI + ps * Db * smith1(NoV, ab) / (4.0 * NoV) + pc * Dc * smith1(NoV, ac) / (4.0 * NoV);
    return f * NoL;
}

void basis(vec3 n, out vec3 t, out vec3 b) {
    float s = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float c = n.x * n.y * a;
    t       = vec3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
    b       = vec3(c, s + n.y * n.y * a, -n.y);
}

// A microfacet normal of GGX seen from v (Heitz 2018), in the tangent space of z.
vec3 sampleVndf(vec3 v, float alpha, float u1, float u2) {
    vec3 vh    = normalize(vec3(alpha * v.x, alpha * v.y, v.z));
    float len2 = vh.x * vh.x + vh.y * vh.y;
    vec3 t1    = len2 > 0.0 ? vec3(-vh.y, vh.x, 0.0) * inversesqrt(len2) : vec3(1, 0, 0);
    vec3 t2    = cross(vh, t1);
    float r = sqrt(u1), phi = 2.0 * PI * u2;
    float p1 = r * cos(phi), p2 = r * sin(phi);
    float s = 0.5 * (1.0 + vh.z);
    p2      = (1.0 - s) * sqrt(max(0.0, 1.0 - p1 * p1)) + s * p2;
    vec3 nh = p1 * t1 + p2 * t2 + sqrt(max(0.0, 1.0 - p1 * p1 - p2 * p2)) * vh;
    return normalize(vec3(alpha * nh.x, alpha * nh.y, max(1e-6, nh.z)));
}

vec3 sampleBsdf(Material m, vec3 n, vec3 v, out vec3 l, out float pdf) {
    vec3 t, b;
    basis(n, t, b);
    vec3 vt   = vec3(dot(v, t), dot(v, b), dot(v, n));
    float NoV = max(vt.z, 1e-4);
    float pd, ps, pc;
    lobes(m, NoV, pd, ps, pc);
    float pick = rnd(), u1 = rnd(), u2 = rnd();
    if (pick < pd) {
        float r = sqrt(u1), phi = 2.0 * PI * u2;
        vec3 lt = vec3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u1)));
        l       = lt.x * t + lt.y * b + lt.z * n;
    } else {
        float rough = pick < pd + ps ? m.rough : m.coatRough;
        vec3 h      = sampleVndf(vt, rough * rough, u1, u2);
        vec3 lt     = reflect(-vt, h);
        l           = lt.x * t + lt.y * b + lt.z * n;
    }
    return evalBsdf(m, n, v, l, pdf);
}

// ---------------------------------------------------------------------------------------------------------------
// Lights

int pickLight(float u) {
    for (int i = 0; i < uLightCount - 1; ++i)
        if (u < uLightCdf[i]) return i;
    return uLightCount - 1;
}
float lightProbability(int i) { return uLightCdf[i] - (i > 0 ? uLightCdf[i - 1] : 0.0); }

// The pdf in solid angle of reaching light i at distance t along rd by sampling the lights.
float lightPdf(int i, vec3 rd, float t) {
    vec3 n     = normalize(cross(uLightU[i], uLightV[i]));
    float cosL = abs(dot(rd, n));
    float area = length(uLightU[i]) * length(uLightV[i]);
    return lightProbability(i) * t * t / max(cosL * area, 1e-8);
}

vec3 lightRadiance(int i, vec3 rd) {
    vec3 n = normalize(cross(uLightU[i], uLightV[i]));
    // A soft box gives its light forwards; the back of it is dark.
    return dot(rd, n) < 0.0 ? uLightL[i] : vec3(0.0);
}

// ---------------------------------------------------------------------------------------------------------------

vec3 trace(vec3 ro, vec3 rd) {
    vec3 radiance   = vec3(0.0);
    vec3 throughput = vec3(1.0);
    float lastPdf   = 0.0;
    bool seeLights  = false;
    for (int bounce = 0; bounce <= uBounces; ++bounce) {
        Hit h;
        // The camera does not see the lights, nor does the studio's floor mirror them, as a photographer keeps them
        // out of the picture: they light the board and shine in its varnish.
        if (!intersect(ro, rd, seeLights, h)) {
            radiance += throughput * ambient(rd);
            if (bounce == 0) gNormal = -rd;
            break;
        }
        if (h.mat == MAT_LIGHT) {  // never on the first hit
            vec3 le  = lightRadiance(h.id, rd);
            float pl = lightPdf(h.id, rd, h.t);
            float w  = lastPdf * lastPdf / (lastPdf * lastPdf + pl * pl);
            radiance += throughput * le * w;
            break;
        }

        vec3 p = ro + rd * h.t;
        vec3 v = -rd;
        if (dot(h.ng, v) < 0.0) h.ng = -h.ng, h.n = -h.n;
        if (dot(h.n, v) < 0.0) h.n = normalize(h.n + h.ng * (0.01 - dot(h.n, v)));

        Material m;
        if (h.mat == MAT_ROOM) {
            vec3 glow;
            m = roomMaterial(p, h.n, glow);
            radiance += throughput * glow;
        } else if (h.mat == MAT_BOARD) m = boardMaterial(p, h.ng);
        else m = pieceMaterial(h.local, uInstInfo[h.id].y > 0.5);
        if (bounce == 0) {
            // Demodulated by this, the denoiser keeps the wood's grain sharp; the floor keeps ebony's reflections.
            gAlbedo = max(m.albedo, vec3(0.08));
            gNormal = h.n;
            gDepth  = h.t;
        }

        vec3 origin = p + h.ng * EPS;

        // Next event: a point on one light, weighed against the BSDF's own sampling where that can reach the light
        // too. The studio's floor takes the lights without its clear coat: the light, not its reflection.
        bool room = h.mat == MAT_ROOM;
        if (uLightCount > 0) {
            int i = pickLight(rnd());
            vec3 wi, le;
            float d, pl;
            if (uLightSun[i] > 0.0) {  // a direction within the sun's disc, uniform in solid angle
                float c = mix(1.0, uLightSun[i], rnd()), s = sqrt(max(0.0, 1.0 - c * c)), phi = 2.0 * PI * rnd();
                vec3 t, b;
                basis(uLightC[i], t, b);
                wi = normalize(c * uLightC[i] + s * cos(phi) * t + s * sin(phi) * b);
                d  = 1e4;
                le = uLightL[i];
                pl = lightProbability(i) / (2.0 * PI * (1.0 - uLightSun[i]));
            } else {
                vec3 q = uLightC[i] + (rnd() - 0.5) * uLightU[i] + (rnd() - 0.5) * uLightV[i];
                wi     = q - origin;
                d      = length(wi);
                wi /= d;
                le = lightRadiance(i, wi);
                pl = lightPdf(i, wi, d);
            }
            if (dot(wi, h.ng) > 0.0 && dot(le, le) > 0.0 && !occluded(origin, wi, d * 0.999)) {
                Material lit = m;
                if (room) lit.coat = 0.0;
                float pdfB;
                vec3 f  = evalBsdf(lit, h.n, v, wi, pdfB);
                float w = (room || uLightSun[i] > 0.0) ? 1.0 : pl * pl / (pl * pl + pdfB * pdfB);
                vec3 c  = throughput * f * le * w / pl;
                if (bounce > 0) c = min(c, vec3(8.0));
                radiance += c;
            }
        }

        vec3 wi;
        float pdf;
        vec3 f = sampleBsdf(m, h.n, v, wi, pdf);
        if (pdf <= 1e-8 || dot(wi, h.ng) <= 0.0) break;
        throughput *= f / pdf;
        lastPdf   = pdf;
        seeLights = !room;

        if (bounce >= 2) {
            float survive = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05, 0.95);
            if (rnd() > survive) break;
            throughput /= survive;
        }
        ro = origin;
        rd = wi;
    }
    return radiance;
}

void main() {
    rngState = pcg(uint(gl_FragCoord.x) + pcg(uint(gl_FragCoord.y) + pcg(uint(uSample) * 7919u + 13u)));

    // A random point of the pixel, through a thin lens focused at the target.
    vec2 jitter = vec2(rnd(), rnd());
    vec2 ndc    = ((gl_FragCoord.xy - 0.5 + jitter) / uResolution) * 2.0 - 1.0;
    float aspect = uResolution.x / uResolution.y;
    vec3 dir     = normalize(uCamForward + uCamRight * ndc.x * uTanHalfFov * aspect + uCamUp * ndc.y * uTanHalfFov);
    vec3 origin  = uCamPos;
    if (uAperture > 0.0) {
        vec3 focus = uCamPos + dir * (uFocusDist / dot(dir, uCamForward));
        float r = sqrt(rnd()) * uAperture, phi = 2.0 * PI * rnd();
        origin += uCamRight * r * cos(phi) + uCamUp * r * sin(phi);
        dir = normalize(focus - origin);
    }

    vec3 c = trace(origin, dir);
    if (any(isnan(c)) || any(isinf(c))) c = vec3(0.0);
    outColor  = vec4(c, 1.0);
    float l   = dot(c, vec3(0.2126, 0.7152, 0.0722));
    bool keep = uSample < GUIDE_SAMPLES;
    outGuide  = vec4(keep ? gAlbedo : vec3(0.0), l * l);
    outGeom   = keep ? vec4(gNormal, gDepth) : vec4(0.0);
}
