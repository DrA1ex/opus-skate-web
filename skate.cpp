// ============================================================================
//   CONCRETE JUNGLE  --  a single-file 3D street skateboarding game
//   Early-2000s New York City block: storefronts, brownstone stoops, a plaza
//   fountain, the basketball cage, a construction site and the East River.
//
//   Language : C++17, one file, no external assets (all geometry, textures,
//              fonts, sound effects and music are generated procedurally).
//   Libraries: SDL2 (window, input, audio) + OpenGL 3.3 core. No raylib.
//
//   BUILD
//     Linux  : g++ -O2 -std=c++17 skate.cpp -o skate $(sdl2-config --cflags --libs) -lGL
//     macOS  : clang++ -O2 -std=c++17 skate.cpp -o skate $(sdl2-config --cflags --libs) -framework OpenGL
//     Windows: g++ -O2 -std=c++17 skate.cpp -o skate.exe -lmingw32 -lSDL2main -lSDL2 -lopengl32
//   RUN
//     ./skate                 options: --fullscreen  --mute  --res 1600x900
//
//   CONTROLS (also shown in-game -- press H)
//     W / Up ............ push (accelerate)          S / Down ...... brake
//     A D / Left Right .. steer | spin in the air | balance on rails
//     SPACE ............. ollie (hold to crouch, release to pop: longer = higher)
//     J or Z + dir ...... flip tricks (kickflip, heelflip, shove-it, impossible, 360 flip)
//     K or X + dir ...... grab tricks (hold to keep grabbing -- let go before landing!)
//     L or C + dir ...... grind / slide when near a rail, ledge, bench or curb
//     I or Shift (+W) ... manual / nose manual (W/S keep balance) -- links combos
//     R reset  H help  M music  V camera  T 2-minute session  Esc pause  F11 fullscreen
// ============================================================================
#ifdef __EMSCRIPTEN__
#include <SDL2/SDL.h>
#include <GLES3/gl3.h>
#include <emscripten.h>
#else
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#else
#include <SDL.h>
#include <SDL_opengl.h>
#endif
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>

#ifndef APIENTRY
#define APIENTRY
#endif

// ----------------------------------------------------------------------------
// OpenGL 2.0+ entry points, loaded at runtime through SDL (called as gl.Name).
// ----------------------------------------------------------------------------
#define GLFUNCS(X)                                                                 \
    X(GLuint, CreateShader, (GLenum))                                              \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))   \
    X(void, CompileShader, (GLuint))                                               \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*))                                 \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                \
    X(GLuint, CreateProgram, (void))                                               \
    X(void, AttachShader, (GLuint, GLuint))                                        \
    X(void, LinkProgram, (GLuint))                                                 \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*))                                \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))               \
    X(void, UseProgram, (GLuint))                                                  \
    X(void, DeleteShader, (GLuint))                                                \
    X(void, BindAttribLocation, (GLuint, GLuint, const GLchar*))                   \
    X(GLint, GetUniformLocation, (GLuint, const GLchar*))                          \
    X(void, Uniform1i, (GLint, GLint))                                             \
    X(void, Uniform1f, (GLint, GLfloat))                                           \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat))                                  \
    X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat))                         \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))                \
    X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*))         \
    X(void, GenBuffers, (GLsizei, GLuint*))                                        \
    X(void, BindBuffer, (GLenum, GLuint))                                          \
    X(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum))                 \
    X(void, BufferSubData, (GLenum, GLintptr, GLsizeiptr, const void*))            \
    X(void, GenVertexArrays, (GLsizei, GLuint*))                                   \
    X(void, BindVertexArray, (GLuint))                                             \
    X(void, EnableVertexAttribArray, (GLuint))                                     \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
    X(void, GenFramebuffers, (GLsizei, GLuint*))                                   \
    X(void, BindFramebuffer, (GLenum, GLuint))                                     \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))         \
    X(GLenum, CheckFramebufferStatus, (GLenum))                                    \
    X(void, ActiveTexture, (GLenum))                                               \
    X(void, GenerateMipmap, (GLenum))

struct GLApi {
#define X(ret, name, args) ret (APIENTRY* name) args = nullptr;
    GLFUNCS(X)
#undef X
    bool load() {
        bool ok = true;
#define X(ret, name, args)                                                     \
    name = (ret (APIENTRY*) args)SDL_GL_GetProcAddress("gl" #name);           \
    if (!name) { fprintf(stderr, "Missing GL function gl%s\n", #name); ok = false; }
        GLFUNCS(X)
#undef X
        return ok;
    }
} gl;

// ----------------------------------------------------------------------------
// Math
// ----------------------------------------------------------------------------
static const float PI = 3.14159265358979f;
static const float TAU = 6.28318530717959f;

struct V2 {
    float x = 0, y = 0;
    V2() {}
    V2(float a, float b) : x(a), y(b) {}
};
inline V2 operator+(V2 a, V2 b) { return {a.x + b.x, a.y + b.y}; }
inline V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
inline V2 operator*(V2 a, float s) { return {a.x * s, a.y * s}; }
inline float dot2(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
inline float len2(V2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }

struct V3 {
    float x = 0, y = 0, z = 0;
    V3() {}
    V3(float a, float b, float c) : x(a), y(b), z(c) {}
    V3& operator+=(V3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    V3& operator-=(V3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    V3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator-(V3 a) { return {-a.x, -a.y, -a.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline V3 operator*(float s, V3 a) { return {a.x * s, a.y * s, a.z * s}; }
inline V3 operator/(V3 a, float s) { return {a.x / s, a.y / s, a.z / s}; }
inline V3 mulv(V3 a, V3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float len(V3 a) { return std::sqrt(dot(a, a)); }
inline float lenXZ(V3 a) { return std::sqrt(a.x * a.x + a.z * a.z); }
inline V3 norm(V3 a) { float l = len(a); return l > 1e-8f ? a / l : V3(0, 1, 0); }
inline V3 lerp3(V3 a, V3 b, float t) { return a + (b - a) * t; }

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float sat(float v) { return clampf(v, 0.f, 1.f); }
inline float smooth01(float t) { t = sat(t); return t * t * (3 - 2 * t); }
inline float signf(float v) { return v < 0 ? -1.f : 1.f; }
inline float wrapPi(float a) {
    while (a > PI) a -= TAU;
    while (a < -PI) a += TAU;
    return a;
}
// frame-rate independent exponential approach
inline float damp(float cur, float target, float rate, float dt) { return target + (cur - target) * std::exp(-rate * dt); }
inline V3 damp3(V3 cur, V3 target, float rate, float dt) { return target + (cur - target) * std::exp(-rate * dt); }
inline float approach(float cur, float target, float step) {
    if (cur < target) return std::min(cur + step, target);
    return std::max(cur - step, target);
}
inline V3 fwdYaw(float yaw) { return {std::sin(yaw), 0, std::cos(yaw)}; }
inline float yawOf(V3 d) { return std::atan2(d.x, d.z); }

// Column-major 4x4 matrix (OpenGL layout): m[col*4 + row]
struct M4 {
    float m[16];
    static M4 ident() { M4 r; for (int i = 0; i < 16; i++) r.m[i] = (i % 5 == 0) ? 1.f : 0.f; return r; }
};
inline M4 operator*(const M4& a, const M4& b) {
    M4 r;
    for (int c = 0; c < 4; c++)
        for (int rw = 0; rw < 4; rw++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + rw] * b.m[c * 4 + k];
            r.m[c * 4 + rw] = s;
        }
    return r;
}
inline V3 xPoint(const M4& a, V3 p) {
    return {a.m[0] * p.x + a.m[4] * p.y + a.m[8] * p.z + a.m[12],
            a.m[1] * p.x + a.m[5] * p.y + a.m[9] * p.z + a.m[13],
            a.m[2] * p.x + a.m[6] * p.y + a.m[10] * p.z + a.m[14]};
}
inline V3 xDir(const M4& a, V3 p) {
    return {a.m[0] * p.x + a.m[4] * p.y + a.m[8] * p.z,
            a.m[1] * p.x + a.m[5] * p.y + a.m[9] * p.z,
            a.m[2] * p.x + a.m[6] * p.y + a.m[10] * p.z};
}
inline M4 mTranslate(V3 t) { M4 r = M4::ident(); r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r; }
inline M4 mScale(V3 s) { M4 r = M4::ident(); r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; return r; }
inline M4 mRotY(float a) {
    M4 r = M4::ident(); float c = std::cos(a), s = std::sin(a);
    r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c; return r;
}
inline M4 mRotX(float a) {
    M4 r = M4::ident(); float c = std::cos(a), s = std::sin(a);
    r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c; return r;
}
inline M4 mRotZ(float a) {
    M4 r = M4::ident(); float c = std::cos(a), s = std::sin(a);
    r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c; return r;
}
// rotation about an arbitrary unit axis
inline M4 mRotAxis(V3 u, float a) {
    M4 r = M4::ident(); float c = std::cos(a), s = std::sin(a), t = 1 - c;
    r.m[0] = t * u.x * u.x + c;       r.m[4] = t * u.x * u.y - s * u.z; r.m[8] = t * u.x * u.z + s * u.y;
    r.m[1] = t * u.x * u.y + s * u.z; r.m[5] = t * u.y * u.y + c;       r.m[9] = t * u.y * u.z - s * u.x;
    r.m[2] = t * u.x * u.z - s * u.y; r.m[6] = t * u.y * u.z + s * u.x; r.m[10] = t * u.z * u.z + c;
    return r;
}
// matrix whose columns are the given axes + origin
inline M4 mBasis(V3 ax, V3 ay, V3 az, V3 o) {
    M4 r = M4::ident();
    r.m[0] = ax.x; r.m[1] = ax.y; r.m[2] = ax.z;
    r.m[4] = ay.x; r.m[5] = ay.y; r.m[6] = ay.z;
    r.m[8] = az.x; r.m[9] = az.y; r.m[10] = az.z;
    r.m[12] = o.x; r.m[13] = o.y; r.m[14] = o.z;
    return r;
}
inline M4 mPerspective(float fovy, float aspect, float n, float f) {
    M4 r; memset(r.m, 0, sizeof(r.m));
    float t = 1.f / std::tan(fovy * 0.5f);
    r.m[0] = t / aspect; r.m[5] = t; r.m[10] = (f + n) / (n - f); r.m[11] = -1; r.m[14] = 2 * f * n / (n - f);
    return r;
}
inline M4 mOrtho(float l, float rr, float b, float t, float n, float f) {
    M4 r = M4::ident();
    r.m[0] = 2 / (rr - l); r.m[5] = 2 / (t - b); r.m[10] = -2 / (f - n);
    r.m[12] = -(rr + l) / (rr - l); r.m[13] = -(t + b) / (t - b); r.m[14] = -(f + n) / (f - n);
    return r;
}
inline M4 mLookAt(V3 eye, V3 at, V3 up) {
    V3 f = norm(at - eye), s = norm(cross(f, up)), u = cross(s, f);
    M4 r = M4::ident();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot(s, eye); r.m[13] = -dot(u, eye); r.m[14] = dot(f, eye);
    return r;
}
inline M4 mInverse(const M4& mat) {
    const float* m = mat.m; float inv[16];
    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    M4 r; det = (std::fabs(det) > 1e-12f) ? 1.f / det : 0.f;
    for (int i = 0; i < 16; i++) r.m[i] = inv[i] * det;
    return r;
}

// Small deterministic RNG + hashes (so the city looks the same every run)
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed = 1234567u) : s(seed ? seed : 1u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (next() & 0xFFFFFF) / 16777216.f; }
    float range(float a, float b) { return a + (b - a) * f(); }
    int irange(int a, int b) { return a + (int)(next() % (uint32_t)(b - a + 1)); }
    bool chance(float p) { return f() < p; }
};
inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
inline float hashf(int a, int b = 0, int c = 0) {
    return (hash32((uint32_t)a * 73856093u ^ (uint32_t)b * 19349663u ^ (uint32_t)c * 83492791u) & 0xFFFFFF) / 16777216.f;
}

struct Col {
    uint8_t r = 255, g = 255, b = 255;
    Col() {}
    Col(int R, int G, int B) : r((uint8_t)std::min(255, std::max(0, R))), g((uint8_t)std::min(255, std::max(0, G))), b((uint8_t)std::min(255, std::max(0, B))) {}
};
inline Col hexc(uint32_t h) { return Col((h >> 16) & 255, (h >> 8) & 255, h & 255); }
inline Col shade(Col c, float k) { return Col((int)(c.r * k), (int)(c.g * k), (int)(c.b * k)); }
inline Col mixc(Col a, Col b, float t) {
    return Col((int)lerpf(a.r, b.r, t), (int)lerpf(a.g, b.g, t), (int)lerpf(a.b, b.b, t));
}

// ----------------------------------------------------------------------------
// Embedded 5x7 pixel font (ASCII 32..126, lowercase drawn as small caps)
// ----------------------------------------------------------------------------
static const uint8_t FONT5x7[95][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A}, {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, {0x18,0x19,0x02,0x04,0x08,0x13,0x03},
    {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, {0x04,0x04,0x00,0x00,0x00,0x00,0x00}, {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},
    {0x00,0x00,0x00,0x00,0x0C,0x04,0x08}, {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, {0x00,0x01,0x02,0x04,0x08,0x10,0x00}, {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08}, {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00},
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, {0x0E,0x11,0x01,0x0D,0x15,0x15,0x0E}, {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E},
    {0x00,0x10,0x08,0x04,0x02,0x01,0x00}, {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, {0x08,0x04,0x00,0x00,0x00,0x00,0x00}, {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, {0x03,0x04,0x04,0x08,0x04,0x04,0x03}, {0x04,0x04,0x04,0x04,0x04,0x04,0x04}, {0x18,0x04,0x04,0x02,0x04,0x04,0x18}, {0x00,0x00,0x08,0x15,0x02,0x00,0x00},
};

static inline bool glyphPixel(int ch, int x, int y) {
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 32 || ch > 126) ch = '?';
    return (FONT5x7[ch - 32][y] >> (4 - x)) & 1;
}

// ----------------------------------------------------------------------------
// Mesh building
// ----------------------------------------------------------------------------
enum Mat : uint8_t {
    MAT_PLAIN = 0, MAT_BRICK, MAT_WINDOWS, MAT_STONEWIN, MAT_GLASSWALL, MAT_ASPHALT, MAT_SIDEWALK,
    MAT_EMISSIVE, MAT_WOOD, MAT_METAL, MAT_FOLIAGE, MAT_FENCE, MAT_SHOPGLASS, MAT_PAVERS, MAT_AWNING,
    MAT_SKIN, MAT_COURT, MAT_CONCRETE, MAT_GRANITE, MAT_CLOTH, MAT_PAINTED, MAT_WATER, MAT_ROOF, MAT_BRICKBANK
};

struct Vtx {
    float p[3];
    float n[3];
    uint8_t c[4];  // rgb + material id
};

struct MeshBuilder {
    std::vector<Vtx> v;
    std::vector<uint32_t> idx;
    void clear() { v.clear(); idx.clear(); }
    uint32_t vert(V3 p, V3 n, Col c, uint8_t mat) {
        Vtx t;
        t.p[0] = p.x; t.p[1] = p.y; t.p[2] = p.z;
        t.n[0] = n.x; t.n[1] = n.y; t.n[2] = n.z;
        t.c[0] = c.r; t.c[1] = c.g; t.c[2] = c.b; t.c[3] = mat;
        v.push_back(t);
        return (uint32_t)v.size() - 1;
    }
    // a,b,c,d counter-clockwise when seen from the front
    void quadN(V3 a, V3 b, V3 c, V3 d, V3 n, Col col, uint8_t mat) {
        uint32_t i = vert(a, n, col, mat); vert(b, n, col, mat); vert(c, n, col, mat); vert(d, n, col, mat);
        idx.insert(idx.end(), {i, i + 1, i + 2, i, i + 2, i + 3});
    }
    void quad(V3 a, V3 b, V3 c, V3 d, Col col, uint8_t mat) { quadN(a, b, c, d, norm(cross(b - a, c - a)), col, mat); }
    // quad that faces towards 'out' regardless of the given winding
    void quadOut(V3 a, V3 b, V3 c, V3 d, V3 out, Col col, uint8_t mat) {
        V3 n = cross(b - a, c - a);
        if (dot(n, out) < 0) quad(d, c, b, a, col, mat); else quad(a, b, c, d, col, mat);
    }
    void quadSmooth(const V3* p, const V3* n, Col col, uint8_t mat) {
        uint32_t i = (uint32_t)v.size();
        for (int k = 0; k < 4; k++) vert(p[k], n[k], col, mat);
        idx.insert(idx.end(), {i, i + 1, i + 2, i, i + 2, i + 3});
    }
    void tri(V3 a, V3 b, V3 c, Col col, uint8_t mat) {
        V3 n = norm(cross(b - a, c - a));
        uint32_t i = vert(a, n, col, mat); vert(b, n, col, mat); vert(c, n, col, mat);
        idx.insert(idx.end(), {i, i + 1, i + 2});
    }
    // Box centered on the xf origin with half extents h. faces: +X 1,-X 2,+Y 4,-Y 8,+Z 16,-Z 32
    void box(const M4& xf, V3 h, Col col, uint8_t mat, int faces = 63, Col* topCol = nullptr) {
        static const float F[6][9] = {
            {1, 0, 0, 0, 1, 0, 0, 0, 1}, {-1, 0, 0, 0, 0, 1, 0, 1, 0}, {0, 1, 0, 0, 0, 1, 1, 0, 0},
            {0, -1, 0, 1, 0, 0, 0, 0, 1}, {0, 0, 1, 1, 0, 0, 0, 1, 0}, {0, 0, -1, 0, 1, 0, 1, 0, 0}};
        for (int f = 0; f < 6; f++) {
            if (!(faces & (1 << f))) continue;
            V3 n(F[f][0], F[f][1], F[f][2]), u(F[f][3], F[f][4], F[f][5]), w(F[f][6], F[f][7], F[f][8]);
            V3 c = mulv(n, h), U = mulv(u, h), W = mulv(w, h);
            V3 p0 = xPoint(xf, c - U - W), p1 = xPoint(xf, c + U - W), p2 = xPoint(xf, c + U + W), p3 = xPoint(xf, c - U + W);
            Col cc = (f == 2 && topCol) ? *topCol : col;
            quadN(p0, p1, p2, p3, norm(xDir(xf, n)), cc, mat);
        }
    }
    void boxAA(V3 mn, V3 mx, Col col, uint8_t mat, int faces = 63, Col* topCol = nullptr) {
        box(mTranslate((mn + mx) * 0.5f), (mx - mn) * 0.5f, col, mat, faces, topCol);
    }
    // Oriented box on the ground: center (cx,cz), rotation about Y, from y0 to y1
    void boxRot(float cx, float cz, float rot, float hx, float hz, float y0, float y1, Col col, uint8_t mat,
                int faces = 63, Col* topCol = nullptr) {
        box(mTranslate(V3(cx, (y0 + y1) * 0.5f, cz)) * mRotY(rot), V3(hx, (y1 - y0) * 0.5f, hz), col, mat, faces, topCol);
    }
    // Box stretched between two points (for limbs, poles, rails). 'side' hints the local X direction.
    void limb(V3 a, V3 b, float w, float d, V3 side, Col col, uint8_t mat) {
        V3 ay = b - a; float L = len(ay);
        if (L < 1e-5f) return;
        ay = ay / L;
        V3 ax = side - ay * dot(side, ay);
        if (len(ax) < 1e-4f) ax = std::fabs(ay.y) < 0.9f ? cross(ay, V3(0, 1, 0)) : cross(ay, V3(1, 0, 0));
        ax = norm(ax);
        V3 az = cross(ax, ay);
        box(mBasis(ax, ay, az, (a + b) * 0.5f), V3(w * 0.5f, L * 0.5f, d * 0.5f), col, mat);
    }
    // Cylinder along local Y from 0..h
    void cylinder(const M4& xf, float r, float h, int seg, Col col, uint8_t mat, bool caps = true, float rTop = -1) {
        if (rTop < 0) rTop = r;
        for (int i = 0; i < seg; i++) {
            float a0 = TAU * i / seg, a1 = TAU * (i + 1) / seg;
            V3 d0(std::sin(a0), 0, std::cos(a0)), d1(std::sin(a1), 0, std::cos(a1));
            V3 p[4] = {xPoint(xf, d0 * r), xPoint(xf, d1 * r), xPoint(xf, d1 * rTop + V3(0, h, 0)), xPoint(xf, d0 * rTop + V3(0, h, 0))};
            V3 n[4] = {norm(xDir(xf, d0)), norm(xDir(xf, d1)), norm(xDir(xf, d1)), norm(xDir(xf, d0))};
            quadSmooth(p, n, col, mat);
            if (caps) {
                tri(xPoint(xf, V3(0, h, 0)), xPoint(xf, d0 * rTop + V3(0, h, 0)), xPoint(xf, d1 * rTop + V3(0, h, 0)), col, mat);
                tri(xPoint(xf, V3(0, 0, 0)), xPoint(xf, d1 * r), xPoint(xf, d0 * r), col, mat);
            }
        }
    }
    // Low-poly ellipsoid
    void sphere(const M4& xf, V3 rad, int seg, int rings, Col col, uint8_t mat) {
        auto P = [&](int i, int j) {
            float th = PI * i / rings, ph = TAU * j / seg;
            return V3(std::sin(th) * std::sin(ph), std::cos(th), std::sin(th) * std::cos(ph));
        };
        for (int i = 0; i < rings; i++)
            for (int j = 0; j < seg; j++) {
                V3 u[4] = {P(i, j), P(i + 1, j), P(i + 1, j + 1), P(i, j + 1)};
                V3 p[4], n[4];
                for (int k = 0; k < 4; k++) {
                    p[k] = xPoint(xf, mulv(u[k], rad));
                    n[k] = norm(xDir(xf, V3(u[k].x / rad.x, u[k].y / rad.y, u[k].z / rad.z)));
                }
                quadSmooth(p, n, col, mat);
            }
    }
    // Pixel text on a plane: 'right' and 'up' are unit vectors, 'px' pixel size. Returns width.
    float text3D(const std::string& s, V3 origin, V3 right, V3 up, float px, Col col, uint8_t mat, float depth = 0) {
        V3 n = norm(cross(right, up));
        float x = 0;
        for (char ch : s) {
            for (int gy = 0; gy < 7; gy++)
                for (int gx = 0; gx < 5; gx++) {
                    if (!glyphPixel((unsigned char)ch, gx, gy)) continue;
                    V3 o = origin + right * ((x + gx) * px) + up * ((6 - gy) * px);
                    if (depth > 0) {
                        M4 xf = mBasis(right, up, n, o + right * (px * 0.5f) + up * (px * 0.5f) + n * (depth * 0.5f));
                        box(xf, V3(px * 0.5f, px * 0.5f, depth * 0.5f), col, mat, 63 & ~32);
                    } else {
                        quadN(o, o + right * px, o + right * px + up * px, o + up * px, n, col, mat);
                    }
                }
            x += 6;
        }
        return x * px;
    }
};
inline float textWidth3D(const std::string& s, float px) { return s.size() * 6 * px - px; }

// A mesh living on the GPU
struct GpuMesh {
    GLuint vao = 0, vbo = 0, ebo = 0;
    int count = 0;
    size_t vcap = 0, icap = 0;
    void upload(const MeshBuilder& mb, bool dynamic) {
        if (!vao) {
            gl.GenVertexArrays(1, &vao);
            gl.GenBuffers(1, &vbo);
            gl.GenBuffers(1, &ebo);
            gl.BindVertexArray(vao);
            gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
            gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
            gl.EnableVertexAttribArray(0);
            gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)0);
            gl.EnableVertexAttribArray(1);
            gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)12);
            gl.EnableVertexAttribArray(2);
            gl.VertexAttribPointer(2, 3, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vtx), (void*)24);
            gl.EnableVertexAttribArray(3);
            gl.VertexAttribPointer(3, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(Vtx), (void*)27);
        }
        gl.BindVertexArray(vao);
        gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
        size_t vb = mb.v.size() * sizeof(Vtx), ib = mb.idx.size() * sizeof(uint32_t);
        GLenum usage = dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
        if (vb > vcap || !dynamic) { vcap = std::max(vb, (size_t)64) * (dynamic ? 2 : 1); gl.BufferData(GL_ARRAY_BUFFER, vcap, nullptr, usage); }
        if (vb) gl.BufferSubData(GL_ARRAY_BUFFER, 0, vb, mb.v.data());
        gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        if (ib > icap || !dynamic) { icap = std::max(ib, (size_t)64) * (dynamic ? 2 : 1); gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, icap, nullptr, usage); }
        if (ib) gl.BufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, ib, mb.idx.data());
        count = (int)mb.idx.size();
        gl.BindVertexArray(0);
    }
    void draw() const {
        if (!count) return;
        gl.BindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0);
        gl.BindVertexArray(0);
    }
};

// ----------------------------------------------------------------------------
// Shaders (GLSL 3.30 core). All surface detail is procedural.
// ----------------------------------------------------------------------------
static const char* WORLD_VS = R"(#version 300 es\nprecision highp float;\nprecision highp int;
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec3 aCol;
layout(location=3) in float aMat;
uniform mat4 uVP;
uniform mat4 uLightVP;
uniform vec4 uClip;
out vec3 vPos; out vec3 vNrm; out vec3 vCol; flat out int vMat; out vec4 vLight; out float vClip;
void main(){
  vPos = aPos; vNrm = aNrm; vCol = aCol; vMat = int(aMat + 0.5);
  vLight = uLightVP * vec4(aPos, 1.0);
  vClip = dot(vec4(aPos,1.0), uClip);
  gl_Position = uVP * vec4(aPos, 1.0);
}
)";

// Shared GLSL helpers (noise + sky), prepended to fragment shaders
static const char* GLSL_COMMON = R"(
uniform vec3 uSunDir; uniform vec3 uSunCol; uniform vec3 uSkyTop; uniform vec3 uSkyHorizon;
uniform vec3 uGroundCol; uniform vec3 uFogCol; uniform vec3 uCamPos; uniform float uTime; uniform float uFogDensity;
float hash12(vec2 p){ vec3 p3 = fract(vec3(p.xyx) * .1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float vnoise(vec2 p){ vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
  return mix(mix(hash12(i),hash12(i+vec2(1,0)),f.x), mix(hash12(i+vec2(0,1)),hash12(i+vec2(1,1)),f.x), f.y); }
float fbm(vec2 p){ float s=0.0,a=0.5; for(int i=0;i<4;i++){ s+=a*vnoise(p); p=p*2.03+vec2(1.7,9.2); a*=0.5; } return s; }
vec3 skyColor(vec3 d){
  float h = clamp(d.y, -1.0, 1.0);
  vec3 c = mix(uSkyHorizon, uSkyTop, pow(clamp(h,0.0,1.0), 0.55));
  c = mix(c, uFogCol*0.8, clamp(-h*4.0, 0.0, 1.0));
  float s = max(dot(d, uSunDir), 0.0);
  c += uSunCol * (pow(s, 900.0)*6.0 + pow(s, 12.0)*0.25 + pow(s,3.0)*0.08);
  return c;
}
vec3 applyFog(vec3 col, vec3 p){
  float d = length(p - uCamPos);
  float f = 1.0 - exp(-d * uFogDensity);
  f = f*f*(3.0-2.0*f);
  vec3 fc = uFogCol + uSunCol*0.12*pow(max(dot(normalize(p-uCamPos), uSunDir),0.0), 6.0);
  return mix(col, fc, clamp(f, 0.0, 1.0));
}
vec3 grade(vec3 c){
  c = c / (1.0 + c*0.18);                           // soft shoulder
  float l = dot(c, vec3(0.299,0.587,0.114));
  c = mix(vec3(l), c, 1.08);                         // a touch of saturation
  c *= vec3(1.03, 1.0, 0.95);                        // warm late-afternoon film
  return pow(clamp(c,0.0,1.0), vec3(0.95));
}
)";

static const char* WORLD_FS_MAIN = R"(
in vec3 vPos; in vec3 vNrm; in vec3 vCol; flat in int vMat; in vec4 vLight; in float vClip;
uniform sampler2DShadow uShadow; uniform float uShadowTexel;
out vec4 fragColor;

float shadowAt(vec3 n){
  vec3 p = vLight.xyz / vLight.w * 0.5 + 0.5;
  if(p.x<0.0||p.x>1.0||p.y<0.0||p.y>1.0||p.z>1.0) return 1.0;
  float bias = 0.0006 + 0.0016*(1.0 - clamp(dot(n,uSunDir),0.0,1.0));
  float s = 0.0;
  for(int x=-1;x<=1;x++) for(int y=-1;y<=1;y++)
    s += texture(uShadow, vec3(p.xy + vec2(x,y)*uShadowTexel*1.2, p.z - bias));
  float fade = smoothstep(0.42, 0.5, max(abs(p.x-0.5), abs(p.y-0.5)));
  return mix(s/9.0, 1.0, fade);
}
// returns window mask (0 wall, 1 glass, 2 frame); id = unique window id
float windowMask(vec2 uv, float cellW, float floorH, float y0, float winW, float winH, float sill, out vec2 id, out vec2 local){
  vec2 q = vec2(uv.x / cellW, (uv.y - y0) / floorH);
  id = floor(q);
  vec2 f = fract(q) * vec2(cellW, floorH);
  local = vec2((f.x - (cellW - winW)*0.5)/winW, (f.y - sill)/winH);
  if(uv.y < y0) return 0.0;
  if(local.x < 0.0 || local.x > 1.0 || local.y < 0.0 || local.y > 1.0) return 0.0;
  vec2 e = min(local, 1.0-local) * vec2(winW, winH);
  if(min(e.x,e.y) < 0.07) return 2.0;
  if(abs(local.y-0.55)*winH < 0.035) return 2.0;          // sash bar
  return 1.0;
}
void main(){
  if(vClip < 0.0) discard;
  vec3 n = normalize(vNrm);
  if(!gl_FrontFacing) n = -n;
  vec3 base = vCol;
  float spec = 0.04, shin = 24.0, emit = 0.0, refl = 0.0, wrap = 0.0;
  bool horiz = abs(n.y) > 0.6;
  vec2 fuv = horiz ? vPos.xz : vec2(abs(n.x) > abs(n.z) ? vPos.z : vPos.x, vPos.y);
  vec3 V = normalize(uCamPos - vPos);
  int m = vMat;
  if(m == 1 || m == 2 || m == 23){                        // brick (+ windows)
    vec2 b = m == 23 ? vPos.xz * vec2(4.4, 8.0) : vec2(fuv.x * 4.4, fuv.y * 13.0);
    float row = floor(b.y);
    b.x += mod(row, 2.0) * 0.5;
    vec2 bi = floor(b), bf = fract(b);
    vec2 fwb = fwidth(b);
    float aa = 1.0 - smoothstep(0.18, 0.5, max(fwb.x, fwb.y));   // fade detail out before it aliases
    float mortar = (bf.y < 0.14 || bf.x < 0.05) ? 1.0 : 0.0;
    float v = hash12(bi);
    base *= mix(0.97, 0.82 + 0.3*v, aa);
    base = mix(base, vec3(0.62,0.6,0.56), mix(0.14, mortar*0.85, aa));
    base *= 0.9 + 0.2*fbm(vPos.xz*0.35 + vPos.y*0.2);
    if(m == 2){
      vec2 id, lc; float w = windowMask(fuv, 2.7, 3.3, 4.8, 1.25, 1.85, 0.75, id, lc);
      if(w > 1.5){ base = mix(vec3(0.85,0.83,0.78), vec3(0.18,0.2,0.22), step(0.5, hash12(id*3.1+7.0))); spec = 0.1; }
      else if(w > 0.5){
        float r = hash12(id + floor(vPos.x*0.01)*13.0 + floor(vPos.z*0.01)*7.0);
        vec3 glass = vec3(0.10,0.13,0.17) + vec3(0.05)*step(0.6,lc.y);
        if(r < 0.22) { base = vec3(1.0,0.78,0.45) * (0.55 + 0.4*hash12(id+3.0)); emit = 0.75; }
        else if(r < 0.35) { base = mix(vec3(0.75,0.72,0.62), glass, step(0.35, lc.x) * step(lc.x, 0.65)); }  // curtains
        else { base = glass; refl = 0.55; spec = 0.9; shin = 90.0; }
      }
    }
  } else if(m == 3){                                        // limestone with windows
    float cy = fuv.y / 0.62;
    float course = step(0.94, fract(cy)) * (1.0 - smoothstep(0.15, 0.4, fwidth(cy)));
    base *= (0.9 + 0.12*fbm(fuv*1.3)) * (1.0 - course*0.18);
    vec2 id, lc; float w = windowMask(fuv, 3.0, 3.6, 4.8, 1.4, 2.3, 0.7, id, lc);
    if(w > 1.5){ base = vec3(0.25,0.24,0.22); }
    else if(w > 0.5){
      float r = hash12(id + 17.0);
      if(r < 0.18){ base = vec3(1.0,0.85,0.55)*0.8; emit = 0.7; }
      else { base = vec3(0.1,0.12,0.15); refl = 0.6; spec = 0.9; shin = 90.0; }
    }
  } else if(m == 4){                                        // glass curtain wall
    vec2 q = vec2(fuv.x / 1.6, fuv.y / 3.4);
    vec2 f = fract(q), id = floor(q);
    float mull = (f.x < 0.04 || f.y < 0.03) ? 1.0 : 0.0;
    base = mix(vec3(0.16,0.23,0.30) * (0.85 + 0.3*hash12(id)), vec3(0.55,0.57,0.6), mull);
    if(mull < 0.5){ refl = 0.75; spec = 1.2; shin = 140.0; if(hash12(id*1.7) < 0.05){ base = vec3(0.8,0.78,0.66); emit = 0.35; refl=0.3; } }
  } else if(m == 5){                                        // asphalt
    float nz = fbm(vPos.xz * 0.9);
    base *= 0.78 + 0.35*nz;
    base *= 0.93 + 0.14*vnoise(vPos.xz*9.0);
    float patch = smoothstep(0.62, 0.66, fbm(vPos.xz*0.12 + 4.0));
    base = mix(base, base*0.72, patch);
    float crack = smoothstep(0.006, 0.0, abs(fbm(vPos.xz*0.5+11.0) - 0.5)) * step(0.62, vnoise(vPos.xz*0.3));
    base *= 1.0 - crack*0.25;
  } else if(m == 6){                                        // sidewalk slabs
    vec2 g = vPos.xz / 1.52;
    vec2 gi = floor(g), gf = fract(g);
    vec2 fwg = fwidth(g);
    float aa = 1.0 - smoothstep(0.02, 0.06, max(fwg.x, fwg.y));
    float joint = (gf.x < 0.012 || gf.y < 0.012 || gf.x > 0.988 || gf.y > 0.988) ? 1.0 : 0.0;
    base *= 0.86 + 0.16*hash12(gi) + 0.1*fbm(vPos.xz*2.0);
    base *= 1.0 - joint*0.45*aa;
    vec2 gc = floor(vPos.xz * 3.0);
    if(hash12(gc) > 0.975){ vec2 d = fract(vPos.xz*3.0)-0.5; if(dot(d,d) < 0.06) base *= 0.55; }   // gum
  } else if(m == 7){ emit = 1.0; }
  else if(m == 8){                                          // wooden planks (along x)
    float w = vPos.z / 0.22; float pi = floor(w);
    float aaw = 1.0 - smoothstep(0.12, 0.35, fwidth(w));
    float gap = step(0.93, fract(w)) * aaw;
    float grain = vnoise(vec2(vPos.x*1.5 + pi*7.0, fract(w)*8.0));
    base *= (0.8 + 0.25*hash12(vec2(pi, floor(vPos.x/3.0 + hash12(vec2(pi,1.0))*3.0)))) * (0.85 + 0.25*grain);
    base *= 1.0 - gap*0.6;
  } else if(m == 9){ spec = 0.8; shin = 60.0; base *= 0.9 + 0.1*vnoise(fuv*20.0); }
  else if(m == 10){                                         // foliage
    float nz = fbm(vPos.xz*3.0 + vPos.y*2.0);
    base *= 0.6 + 0.8*nz; wrap = 0.5;
  } else if(m == 11){                                       // chain-link fence (cutout)
    vec2 q = fuv * 14.0;
    vec2 r = vec2(q.x + q.y, q.x - q.y);
    vec2 fwq = fwidth(r);
    if(max(fwq.x, fwq.y) > 0.28){ if(hash12(gl_FragCoord.xy) > 0.33) discard; }   // far away: dithered density
    else { vec2 f = abs(fract(r) - 0.5); if(min(f.x, f.y) > 0.09) discard; }
    spec = 0.6; shin = 40.0;
  } else if(m == 12){                                       // shop window glass
    vec3 R = reflect(-V, n);
    float fr = 0.25 + 0.75*pow(1.0 - max(dot(n, V), 0.0), 3.0);
    vec3 inside = vec3(0.16,0.13,0.1) + vec3(0.5,0.4,0.25)*smoothstep(0.55,0.75,fbm(fuv*vec2(1.5,3.0)));
    inside += vec3(0.6,0.55,0.4)*smoothstep(3.6, 4.2, vPos.y);
    base = mix(inside, skyColor(R)*0.8, fr*0.7);
    emit = 0.55; spec = 1.0; shin = 120.0;
  } else if(m == 13){                                       // brick pavers
    vec2 b = vPos.xz * vec2(5.0, 10.0);
    b.x += mod(floor(b.y), 2.0) * 0.5;
    vec2 bf = fract(b);
    vec2 fwb = fwidth(b);
    float aa = 1.0 - smoothstep(0.15, 0.45, max(fwb.x, fwb.y));
    base *= mix(0.95, 0.8 + 0.3*hash12(floor(b)), aa);
    if(bf.x < 0.06 || bf.y < 0.1) base = mix(base, vec3(0.45,0.42,0.38), 0.7*aa);
    base = mix(base, vec3(0.45,0.42,0.38), 0.12*(1.0-aa));
  } else if(m == 14){                                       // striped awning
    float s = step(0.5, fract(fuv.x / 0.45));
    base = mix(base, vec3(0.92,0.9,0.86), s);
    wrap = 0.3;
  } else if(m == 15){ wrap = 0.35; spec = 0.08; }
  else if(m == 16){ base *= 0.9 + 0.15*fbm(vPos.xz*1.5); }
  else if(m == 17){ base *= 0.85 + 0.25*fbm(fuv*1.7) ; base *= 0.95 + 0.08*vnoise(fuv*14.0); }
  else if(m == 18){ float gn = vnoise(fuv*14.0 + vPos.y*9.0); base *= 0.9 + 0.14*mix(gn, 0.5, smoothstep(0.1, 0.4, fwidth(fuv.x*14.0))); spec = 0.25; shin = 50.0; }
  else if(m == 19){ base *= 0.9 + 0.12*vnoise(fuv*25.0); wrap = 0.25; }
  else if(m == 20){ spec = 0.7; shin = 80.0; refl = 0.25; }
  else if(m == 22){ base *= 0.75 + 0.35*fbm(vPos.xz*0.8); }

  float ndl = dot(n, uSunDir);
  float diff = max((ndl + wrap) / (1.0 + wrap), 0.0);
  float sh = ndl > -0.2 ? shadowAt(n) : 0.0;
  vec3 hemi = mix(uGroundCol, uSkyTop*0.9 + uSkyHorizon*0.2, n.y*0.5 + 0.5);
  float ao = horiz ? 1.0 : mix(0.72, 1.0, smoothstep(0.0, 1.4, vPos.y + 0.1));
  vec3 col = base * (hemi * 0.62 * ao + uSunCol * diff * sh);
  vec3 H = normalize(uSunDir + V);
  col += uSunCol * spec * pow(max(dot(n, H), 0.0), shin) * sh;
  if(refl > 0.0){
    vec3 R = reflect(-V, n);
    float fr = 0.2 + 0.8*pow(1.0 - max(dot(n, V), 0.0), 4.0);
    col = mix(col, skyColor(R) * 0.85, refl * fr);
  }
  col = mix(col, base, emit);
  col = applyFog(col, vPos);
  fragColor = vec4(grade(col), 1.0);
}
)";

static const char* SHADOW_VS = R"(#version 300 es\nprecision highp float;\nprecision highp int;
layout(location=0) in vec3 aPos;
layout(location=3) in float aMat;
uniform mat4 uLightVP;
out vec3 vPos; flat out int vMat;
void main(){ vPos = aPos; vMat = int(aMat+0.5); gl_Position = uLightVP * vec4(aPos,1.0); }
)";
static const char* SHADOW_FS = R"(#version 300 es\nprecision highp float;\nprecision highp int;
in vec3 vPos; flat in int vMat;
void main(){
  if(vMat == 11){            // chain-link casts a diamond shadow
    vec2 q = vec2(vPos.x + vPos.z, vPos.y) * 14.0;
    vec2 r = vec2(q.x + q.y, q.x - q.y);
    vec2 f = abs(fract(r) - 0.5);
    if(min(f.x, f.y) > 0.09) discard;
  }
  if(vMat == 7 && vPos.y > 30.0) discard;   // skyline lights
}
)";

static const char* SKY_VS = R"(#version 300 es\nprecision highp float;\nprecision highp int;
out vec2 vNdc;
void main(){ vec2 p = vec2((gl_VertexID<<1)&2, gl_VertexID&2); vNdc = p*2.0-1.0; gl_Position = vec4(vNdc, 0.9999, 1.0); }
)";
static const char* SKY_FS_MAIN = R"(
in vec2 vNdc; uniform mat4 uInvVP; out vec4 fragColor;
void main(){
  vec4 a = uInvVP * vec4(vNdc, -1.0, 1.0), b = uInvVP * vec4(vNdc, 1.0, 1.0);
  vec3 d = normalize(b.xyz/b.w - a.xyz/a.w);
  vec3 c = skyColor(d);
  if(d.y > 0.0){                                     // drifting clouds
    vec2 uv = d.xz / (d.y + 0.08) * 1.6 + vec2(uTime*0.012, uTime*0.004);
    float cl = smoothstep(0.5, 0.85, fbm(uv*0.8) + 0.15*fbm(uv*3.0));
    vec3 cc = mix(vec3(1.0,0.93,0.85), uSunCol, 0.3) * (0.85 + 0.15*d.y);
    c = mix(c, cc, cl * smoothstep(0.0, 0.25, d.y) * 0.7);
  }
  fragColor = vec4(grade(c), 1.0);
}
)";

// Water: river, fountain pool, puddles. aMat: 0 river, 1 pool, 2 puddle, 3 spray sheet
static const char* WATER_VS = R"(#version 300 es\nprecision highp float;\nprecision highp int;
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec3 aCol;
layout(location=3) in float aMat;
uniform mat4 uVP; uniform float uTime; uniform mat4 uLightVP;
out vec3 vPos; out vec3 vCol; flat out int vKind; out vec4 vLight;
void main(){
  vec3 p = aPos; vKind = int(aMat+0.5);
  if(vKind == 0){
    p.y += 0.12*sin(p.x*0.35 + uTime*1.3) + 0.08*sin(p.z*0.5 - uTime*1.7 + p.x*0.2);
  }
  vPos = p; vCol = aCol; vLight = uLightVP * vec4(p,1.0);
  gl_Position = uVP * vec4(p, 1.0);
}
)";
static const char* WATER_FS_MAIN = R"(
in vec3 vPos; in vec3 vCol; flat in int vKind; in vec4 vLight;
uniform sampler2D uReflTex; uniform vec2 uViewport; uniform int uHasRefl; uniform vec2 uPoolCenter;
out vec4 fragColor;
vec2 waveGrad(vec2 p, float t, float k){
  vec2 g = vec2(0.0);
  g += vec2(0.35, 0.0) * cos(p.x*0.35 + t*1.3) * 0.12;
  g += vec2(0.2*0.08, 0.5*0.08) * cos(p.y*0.5 - t*1.7 + p.x*0.2);
  float e = 0.15;
  float n0 = fbm(p*k + vec2(t*0.35, t*0.2));
  float nx = fbm((p+vec2(e,0.0))*k + vec2(t*0.35, t*0.2));
  float nz = fbm((p+vec2(0.0,e))*k + vec2(t*0.35, t*0.2));
  g += vec2(nx-n0, nz-n0) / e * 0.35;
  return g;
}
void main(){
  float t = uTime;
  vec2 g;
  float alpha = 1.0;
  if(vKind == 0) g = waveGrad(vPos.xz, t, 0.9);
  else if(vKind == 1){
    vec2 d = vPos.xz - uPoolCenter; float r = length(d);
    g = normalize(d + 1e-4) * cos(r*9.0 - t*6.0) * 0.18 + waveGrad(vPos.xz, t, 3.0)*0.25;
  } else {
    g = waveGrad(vPos.xz*2.0, t*0.3, 2.0) * 0.12;
    alpha = vCol.b;                                    // puddle edge fade
  }
  vec3 n = normalize(vec3(-g.x, 1.0, -g.y));
  vec3 V = normalize(uCamPos - vPos);
  vec3 R = reflect(-V, n);
  R.y = abs(R.y);
  float fr = 0.04 + 0.96*pow(1.0 - max(dot(n, V), 0.0), 5.0);
  vec3 refl = skyColor(R);
  if(uHasRefl == 1 && vKind == 0){
    vec2 suv = gl_FragCoord.xy / uViewport + n.xz * 0.035;
    vec3 rt = texture(uReflTex, vec2(suv.x, suv.y)).rgb;
    refl = mix(refl, rt, 0.85);
  }
  vec3 deep = vKind == 0 ? vec3(0.05,0.12,0.13) : (vKind == 1 ? vec3(0.08,0.24,0.28) : vec3(0.08,0.08,0.08));
  vec3 lsh = vLight.xyz / vLight.w * 0.5 + 0.5;
  vec3 col = mix(deep * (0.5 + 0.5*max(uSunDir.y,0.0)), refl, clamp(fr + (vKind==2?0.35:0.1), 0.0, 1.0));
  vec3 H = normalize(uSunDir + V);
  col += uSunCol * pow(max(dot(n, H), 0.0), 220.0) * 3.0;
  if(vKind == 0){ float foam = smoothstep(0.72, 0.9, fbm(vPos.xz*0.6 + vec2(t*0.2, 0.0))); col += vec3(0.5)*foam*0.25; }
  col = applyFog(col, vPos);
  fragColor = vec4(grade(col), vKind == 0 ? 1.0 : (vKind == 1 ? 0.88 : alpha * (0.35 + 0.6*fr)));
}
)";

// Billboard particles (premultiplied alpha: additive when a == 0)
static const char* PART_VS = R"(#version 300 es\nprecision highp float;\nprecision highp int;
layout(location=0) in vec3 aPos; layout(location=1) in vec2 aUV; layout(location=2) in vec4 aCol;
uniform mat4 uVP; out vec2 vUV; out vec4 vCol; out vec3 vPos;
void main(){ vUV = aUV; vCol = aCol; vPos = aPos; gl_Position = uVP * vec4(aPos,1.0); }
)";
static const char* PART_FS_MAIN = R"(
in vec2 vUV; in vec4 vCol; in vec3 vPos; out vec4 fragColor;
void main(){
  vec2 d = vUV*2.0-1.0; float r = dot(d,d);
  if(r > 1.0) discard;
  float a = (1.0 - r);
  float fogk = 1.0 - clamp(exp(-length(vPos-uCamPos)*uFogDensity), 0.0, 1.0);
  vec3 c = vCol.rgb * (1.0 - fogk*0.8);
  fragColor = vec4(c * a, vCol.a * a);
}
)";

// 2D HUD: textured (font atlas) or solid quads
static const char* HUD_VS = R"(#version 300 es\nprecision highp float;\nprecision highp int;
layout(location=0) in vec2 aPos; layout(location=1) in vec2 aUV; layout(location=2) in vec4 aCol;
uniform vec2 uScreen; out vec2 vUV; out vec4 vCol;
void main(){ vUV = aUV; vCol = aCol; gl_Position = vec4(aPos.x/uScreen.x*2.0-1.0, 1.0-aPos.y/uScreen.y*2.0, 0.0, 1.0); }
)";
static const char* HUD_FS = R"(#version 300 es\nprecision highp float;\nprecision highp int;
in vec2 vUV; in vec4 vCol; uniform sampler2D uFont; out vec4 fragColor;
void main(){
  float a = vUV.x < 0.0 ? 1.0 : texture(uFont, vUV).r;
  if(vUV.x < -1.5){ vec2 d = vec2(-vUV.x-3.0, vUV.y); a = smoothstep(0.45, 1.35, length(d)); }   // vignette
  fragColor = vec4(vCol.rgb, vCol.a * a);
}
)";

static GLuint compileShader(const char* name, GLenum type, const std::string& src) {
    GLuint s = gl.CreateShader(type);
    const char* p = src.c_str();
    gl.ShaderSource(s, 1, &p, nullptr);
    gl.CompileShader(s);
    GLint ok = 0;
    gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096]; gl.GetShaderInfoLog(s, sizeof(log), nullptr, log);
        fprintf(stderr, "%s %s shader compile error:\n%s\n", name,
                type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
    }
    return s;
}
static GLuint makeProgram(const char* name, const std::string& vs, const std::string& fs) {
    GLuint p = gl.CreateProgram();
    GLuint a = compileShader(name, GL_VERTEX_SHADER, vs);
    GLuint b = compileShader(name, GL_FRAGMENT_SHADER, fs);
    gl.AttachShader(p, a); gl.AttachShader(p, b);
    gl.LinkProgram(p);
    GLint ok = 0;
    gl.GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096]; gl.GetProgramInfoLog(p, sizeof(log), nullptr, log);
        fprintf(stderr, "%s program link error:\n%s\n", name, log);
    }
    gl.DeleteShader(a); gl.DeleteShader(b);
    return p;
}
static std::string fsWithCommon(const char* body) { return std::string("#version 300 es\nprecision highp float;\nprecision highp int;\n") + GLSL_COMMON + body; }

// ----------------------------------------------------------------------------
// Collision world: oriented boxes, ramps and quarter pipes as a 2.5D height
// field, plus grindable line segments and named gaps.
// ----------------------------------------------------------------------------
enum SolidType { S_BOX = 0, S_RAMP = 1, S_QP = 2 };
enum Surf { SURF_ASPHALT = 0, SURF_CONCRETE, SURF_WOOD, SURF_METAL, SURF_BRICK, SURF_WATER, SURF_COURT, SURF_SIDEWALK };

static const float WATER_LEVEL = -1.9f;     // river surface
static const float RIVER_EDGE_Z = -88.f;    // seawall line (north)
static const float STEP_UP = 0.19f;         // curbs are auto-climbed, stairs are not

struct Solid {
    float cx = 0, cz = 0, c = 1, s = 0;   // center + cos/sin of yaw
    float hx = 1, hz = 1;                 // half extents (local x / local z)
    float y0 = 0, h = 1;                  // bottom and height
    int type = S_BOX;
    float R = 0;                          // quarter-pipe transition radius
    int surf = SURF_CONCRETE;
    bool tall = false;                    // blocks the camera
    bool noWall = false;                  // never treated as a wall
    bool noGround = false;                // never stood on (thin rail blockers)
    void toLocal(float x, float z, float& lx, float& lz) const {
        float dx = x - cx, dz = z - cz;
        lx = dx * c - dz * s;
        lz = dx * s + dz * c;
    }
    float heightLocal(float lx, float lz) const {
        (void)lx;
        if (type == S_BOX) return y0 + h;
        if (type == S_RAMP) return y0 + h * sat((lz + hz) / (2 * hz));
        float x = clampf(lz + hz, 0.f, 2 * hz);
        return y0 + R - std::sqrt(std::max(R * R - x * x, 0.f));
    }
    V3 normalLocal(float lx, float lz) const {
        (void)lx;
        float k = 0;
        if (type == S_RAMP) k = h / (2 * hz);
        else if (type == S_QP) {
            float x = clampf(lz + hz, 0.f, 2 * hz);
            k = x / std::sqrt(std::max(R * R - x * x, 1e-4f));
        }
        if (type == S_BOX) return V3(0, 1, 0);
        V3 nl = norm(V3(0, 1, -k));   // (local x, y, local z)
        // local z axis in world = (s, 0, c)
        return norm(V3(s * nl.z, nl.y, c * nl.z));
    }
    bool inside(float lx, float lz, float m = 0) const { return std::fabs(lx) <= hx + m && std::fabs(lz) <= hz + m; }
    V3 localDirZ() const { return V3(s, 0, c); }
    V3 localDirX() const { return V3(c, 0, -s); }
};

enum RailKind { RK_METAL = 0, RK_LEDGE, RK_CURB, RK_WOOD, RK_CAR };
struct Rail {
    V3 a, b, dir;
    float L = 0;
    int kind = RK_METAL;
};

struct Gap {
    std::string name;
    int points = 100;
    float cx, cz, c, s, hx, hz, topY;
    bool inside(float x, float z) const {
        float dx = x - cx, dz = z - cz;
        float lx = dx * c - dz * s, lz = dx * s + dz * c;
        return std::fabs(lx) <= hx && std::fabs(lz) <= hz;
    }
};

struct Pool { float x, z, r, floorY; };   // fountain basins: landing inside = splash

struct GroundHit {
    float h = 0;
    V3 n = V3(0, 1, 0);
    int solid = -1;
    int surf = SURF_ASPHALT;
};

struct World {
    std::vector<Solid> solids;
    std::vector<Rail> rails;
    std::vector<Gap> gaps;
    std::vector<Pool> pools;
    // uniform grid for solids
    static constexpr float GX0 = -170, GZ0 = -200, CELL = 4;
    static constexpr int GW = 85, GH = 95;
    std::vector<std::vector<int>> grid;
    float minX = -69.f, maxX = 69.f, minZ = -100.f, maxZ = 69.f;   // playable bounds

    int addSolid(const Solid& s) {
        solids.push_back(s);
        return (int)solids.size() - 1;
    }
    int addBox(float cx, float cz, float rot, float hx, float hz, float y0, float y1, int surf, bool tall = false) {
        Solid s; s.cx = cx; s.cz = cz; s.c = std::cos(rot); s.s = std::sin(rot);
        s.hx = hx; s.hz = hz; s.y0 = y0; s.h = y1 - y0; s.type = S_BOX; s.surf = surf; s.tall = tall;
        return addSolid(s);
    }
    // ramp rising towards local +z (yaw 'rot' => rises along fwdYaw(rot))
    int addRamp(float cx, float cz, float rot, float hx, float hz, float y0, float height, int surf) {
        Solid s; s.cx = cx; s.cz = cz; s.c = std::cos(rot); s.s = std::sin(rot);
        s.hx = hx; s.hz = hz; s.y0 = y0; s.h = height; s.type = S_RAMP; s.surf = surf;
        return addSolid(s);
    }
    // quarter pipe of transition radius R and height H, rising towards fwdYaw(rot); (cx,cz) = footprint center
    int addQP(float cx, float cz, float rot, float hx, float R, float H, float y0, int surf) {
        Solid s; s.cx = cx; s.cz = cz; s.c = std::cos(rot); s.s = std::sin(rot);
        float W = std::sqrt(std::max(R * R - (R - H) * (R - H), 0.01f));
        s.hx = hx; s.hz = W * 0.5f; s.y0 = y0; s.h = H; s.R = R; s.type = S_QP; s.surf = surf;
        return addSolid(s);
    }
    void addRail(V3 a, V3 b, int kind) {
        Rail r; r.a = a; r.b = b; r.L = len(b - a);
        if (r.L < 0.3f) return;
        r.dir = (b - a) / r.L; r.kind = kind;
        rails.push_back(r);
    }
    void addGap(const std::string& name, int pts, float cx, float cz, float rot, float hx, float hz, float topY) {
        Gap g; g.name = name; g.points = pts; g.cx = cx; g.cz = cz; g.c = std::cos(rot); g.s = std::sin(rot);
        g.hx = hx; g.hz = hz; g.topY = topY;
        gaps.push_back(g);
    }
    void buildGrid() {
        grid.assign(GW * GH, {});
        for (int i = 0; i < (int)solids.size(); i++) {
            const Solid& s = solids[i];
            float ex = std::fabs(s.c) * s.hx + std::fabs(s.s) * s.hz;
            float ez = std::fabs(s.s) * s.hx + std::fabs(s.c) * s.hz;
            int x0 = (int)std::floor((s.cx - ex - 1 - GX0) / CELL), x1 = (int)std::floor((s.cx + ex + 1 - GX0) / CELL);
            int z0 = (int)std::floor((s.cz - ez - 1 - GZ0) / CELL), z1 = (int)std::floor((s.cz + ez + 1 - GZ0) / CELL);
            for (int z = std::max(z0, 0); z <= std::min(z1, GH - 1); z++)
                for (int x = std::max(x0, 0); x <= std::min(x1, GW - 1); x++) grid[z * GW + x].push_back(i);
        }
    }
    const std::vector<int>* cell(float x, float z) const {
        static const std::vector<int> empty;
        int gx = (int)std::floor((x - GX0) / CELL), gz = (int)std::floor((z - GZ0) / CELL);
        if (gx < 0 || gz < 0 || gx >= GW || gz >= GH) return &empty;
        return &grid[gz * GW + gx];
    }
    float baseGround(float x, float z) const { (void)x; return z < RIVER_EDGE_Z ? -40.f : 0.f; }

    // Highest walkable surface at (x,z) that is not above yRef.
    GroundHit ground(float x, float z, float yRef) const {
        GroundHit g;
        g.h = baseGround(x, z);
        g.surf = z < RIVER_EDGE_Z ? SURF_WATER : SURF_ASPHALT;
        for (int i : *cell(x, z)) {
            const Solid& s = solids[i];
            if (s.y0 > yRef + 0.05f || s.noGround) continue;
            float lx, lz; s.toLocal(x, z, lx, lz);
            if (!s.inside(lx, lz)) continue;
            float h = s.heightLocal(lx, lz);
            if (h > yRef || h < g.h) continue;
            g.h = h; g.solid = i; g.surf = s.surf; g.n = s.normalLocal(lx, lz);
        }
        for (const Pool& p : pools) {
            float dx = x - p.x, dz = z - p.z;
            if (dx * dx + dz * dz < p.r * p.r && p.floorY <= yRef && p.floorY >= g.h - 0.2f) {
                g.h = std::max(g.h, p.floorY); g.surf = SURF_WATER; g.solid = -2; g.n = V3(0, 1, 0);
            }
        }
        return g;
    }
    // Slow ground query over all solids (used while the level is being built, before the grid exists)
    float groundSlow(float x, float z, float yRef) const {
        float best = baseGround(x, z);
        for (const Solid& s : solids) {
            if (s.y0 > yRef + 0.05f || s.noGround) continue;
            float lx, lz; s.toLocal(x, z, lx, lz);
            if (!s.inside(lx, lz)) continue;
            float h = s.heightLocal(lx, lz);
            if (h <= yRef && h > best) best = h;
        }
        return best;
    }
    // Push a vertical capsule (radius r, from y..y+height) out of any solid whose surface is above 'stepTop'.
    // Returns the strongest impact speed into a wall (0 if none); 'hitN' receives the wall normal.
    float collideWalls(V3& pos, V3& vel, float r, float stepTop, float height, V3* hitN = nullptr) const {
        float worst = 0;
        for (int iter = 0; iter < 3; iter++) {
            bool any = false;
            std::vector<int> cand;
            for (int ox = -1; ox <= 1; ox += 2)
                for (int oz = -1; oz <= 1; oz += 2)
                    for (int i : *cell(pos.x + ox * r, pos.z + oz * r))
                        if (std::find(cand.begin(), cand.end(), i) == cand.end()) cand.push_back(i);
            for (int i : cand) {
                const Solid& s = solids[i];
                if (s.noWall) continue;
                if (s.y0 > pos.y + height) continue;
                float lx, lz; s.toLocal(pos.x, pos.z, lx, lz);
                if (!s.inside(lx, lz, r)) continue;
                float clx = clampf(lx, -s.hx, s.hx), clz = clampf(lz, -s.hz, s.hz);
                float hs = s.heightLocal(clx, clz);
                if (hs <= stepTop) continue;
                // local push direction
                float px, pz, pen;
                float ddx = lx - clx, ddz = lz - clz, dd = std::sqrt(ddx * ddx + ddz * ddz);
                if (dd > 1e-5f) { px = ddx / dd; pz = ddz / dd; pen = r - dd; }
                else {
                    float penx = s.hx + r - std::fabs(lx), penz = s.hz + r - std::fabs(lz);
                    if (penx < penz) { px = signf(lx); pz = 0; pen = penx; }
                    else { px = 0; pz = signf(lz); pen = penz; }
                }
                if (pen <= 0) continue;
                // local -> world direction
                V3 nW(px * s.c + pz * s.s, 0, -px * s.s + pz * s.c);
                pos += nW * (pen + 0.001f);
                float vn = dot(vel, nW);
                if (vn < 0) {
                    vel -= nW * vn * 1.1f;
                    if (-vn > worst) { worst = -vn; if (hitN) *hitN = nW; }
                }
                any = true;
            }
            if (!any) break;
        }
        // world bounds (invisible walls)
        if (pos.x < minX) { pos.x = minX; if (vel.x < 0) { worst = std::max(worst, -vel.x); vel.x = -vel.x * 0.2f; } }
        if (pos.x > maxX) { pos.x = maxX; if (vel.x > 0) { worst = std::max(worst, vel.x); vel.x = -vel.x * 0.2f; } }
        if (pos.z > maxZ) { pos.z = maxZ; if (vel.z > 0) { worst = std::max(worst, vel.z); vel.z = -vel.z * 0.2f; } }
        if (pos.z < minZ) { pos.z = minZ; if (vel.z < 0) { vel.z = -vel.z * 0.2f; } }
        return worst;
    }
    // Is the point inside any solid (used for camera and particles)?
    bool pointBlocked(V3 p, bool tallOnly) const {
        for (int i : *cell(p.x, p.z)) {
            const Solid& s = solids[i];
            if (tallOnly && !s.tall) continue;
            if (p.y < s.y0) continue;
            float lx, lz; s.toLocal(p.x, p.z, lx, lz);
            if (!s.inside(lx, lz, 0.25f)) continue;
            if (p.y < s.heightLocal(clampf(lx, -s.hx, s.hx), clampf(lz, -s.hz, s.hz)) + 0.2f) return true;
        }
        return false;
    }
    // Fraction of the segment a->b that is free of tall solids
    float rayFree(V3 a, V3 b) const {
        V3 d = b - a; float L = len(d);
        int steps = std::max(2, (int)(L / 0.25f));
        for (int i = 1; i <= steps; i++) {
            float t = (float)i / steps;
            if (pointBlocked(a + d * t, true)) return std::max(0.f, (float)(i - 1) / steps);
        }
        return 1.f;
    }
};
static World world;

// ----------------------------------------------------------------------------
// Level construction helpers: every prop emits render geometry (SM / WM) and
// its collision solids, grind rails and gaps.
// ----------------------------------------------------------------------------
static MeshBuilder SM;   // static opaque world
static MeshBuilder WM;   // water surfaces

enum EmitKind { EM_FOUNTAIN = 0, EM_HYDRANT, EM_STEAM, EM_POOLSPLASH };
struct Emitter { V3 pos, dir; int kind; float rate; float acc = 0; };
static std::vector<Emitter> emitters;
struct TrafficLight { V3 pos; float yaw; int axis; };   // axis 0: faces E-W traffic, 1: N-S
static std::vector<TrafficLight> tlights;
static std::vector<V3> pigeonSpots;
struct NpcPath { std::vector<V3> pts; bool loop = true; };
static std::vector<NpcPath> npcPaths;
static std::vector<V3> letterPos;
struct Lamp { V3 pos; };
static std::vector<Lamp> lamps;

// palette
static const Col C_ASPHALT = hexc(0x46464a), C_SIDEWALK = hexc(0xa9a59c), C_CURB = hexc(0x8f8b84);
static const Col C_GRANITE = hexc(0x8d8a86), C_IRON = hexc(0x1e2220), C_STEEL = hexc(0x9aa0a6);
static const Col C_WOOD = hexc(0x8a6a48), C_PLYWOOD = hexc(0xc49a62), C_YELLOW = hexc(0xf2c318), C_WHITE = hexc(0xeeeeea);

static V3 rotLocal(float rot, float lx, float lz) {   // local (x,z) offset -> world offset
    float c = std::cos(rot), s = std::sin(rot);
    return V3(lx * c + lz * s, 0, -lx * s + lz * c);
}
static M4 frame(float cx, float y, float cz, float rot) { return mTranslate(V3(cx, y, cz)) * mRotY(rot); }

static void triOut(MeshBuilder& mb, V3 a, V3 b, V3 c, V3 out, Col col, uint8_t mat) {
    if (dot(cross(b - a, c - a), out) < 0) mb.tri(a, c, b, col, mat); else mb.tri(a, b, c, col, mat);
}

// Solid box with render + collision
static int solidBox(float cx, float cz, float rot, float hx, float hz, float y0, float y1, Col col, uint8_t mat,
                    int surf, bool tall = false, Col* top = nullptr, int faces = 63) {
    SM.boxRot(cx, cz, rot, hx, hz, y0, y1, col, mat, faces, top);
    return world.addBox(cx, cz, rot, hx, hz, y0, y1, surf, tall);
}
static void addRailW(V3 a, V3 b, int kind) { world.addRail(a, b, kind); }

// grind rails along the long top edges (and optionally short ones) of an oriented box
static void edgeRails(float cx, float cz, float rot, float hx, float hz, float top, int kind, bool longOnly = true) {
    V3 c(cx, top, cz);
    V3 ax = rotLocal(rot, 1, 0), az = rotLocal(rot, 0, 1);
    bool xLong = hx >= hz;
    if (xLong || !longOnly) {
        addRailW(c + ax * -hx + az * hz, c + ax * hx + az * hz, kind);
        addRailW(c + ax * -hx + az * -hz, c + ax * hx + az * -hz, kind);
    }
    if (!xLong || !longOnly) {
        addRailW(c + az * -hz + ax * hx, c + az * hz + ax * hx, kind);
        addRailW(c + az * -hz + ax * -hx, c + az * hz + ax * -hx, kind);
    }
}

// Sidewalk / plaza slab with curb rails on chosen edges (bit 1:-x 2:+x 4:-z 8:+z)
static void slab(float x0, float z0, float x1, float z1, float y1, Col col, uint8_t mat, int surf, int curbEdges) {
    SM.boxAA(V3(x0, -0.3f, z0), V3(x1, y1, z1), C_CURB, MAT_CONCRETE, 1 | 2 | 16 | 32);
    SM.quadN(V3(x0, y1, z1), V3(x1, y1, z1), V3(x1, y1, z0), V3(x0, y1, z0), V3(0, 1, 0), col, mat);
    world.addBox((x0 + x1) * 0.5f, (z0 + z1) * 0.5f, 0, (x1 - x0) * 0.5f, (z1 - z0) * 0.5f, -0.3f, y1, surf);
    if (curbEdges & 1) addRailW(V3(x0, y1, z0), V3(x0, y1, z1), RK_CURB);
    if (curbEdges & 2) addRailW(V3(x1, y1, z0), V3(x1, y1, z1), RK_CURB);
    if (curbEdges & 4) addRailW(V3(x0, y1, z0), V3(x1, y1, z0), RK_CURB);
    if (curbEdges & 8) addRailW(V3(x0, y1, z1), V3(x1, y1, z1), RK_CURB);
}
// flat overlay quad (paint, different paving) slightly above a surface
static void overlay(float x0, float z0, float x1, float z1, float y, Col col, uint8_t mat) {
    SM.quadN(V3(x0, y, z1), V3(x1, y, z1), V3(x1, y, z0), V3(x0, y, z0), V3(0, 1, 0), col, mat);
}

// Metal handrail with posts; grindable
static void handrail(V3 a, V3 b, bool grind = true, Col col = C_IRON, float postEvery = 1.8f) {
    SM.limb(a, b, 0.06f, 0.06f, V3(0, 1, 0), col, MAT_METAL);
    V3 d = b - a; float L = lenXZ(d);
    int n = std::max(1, (int)std::ceil(L / postEvery));
    for (int i = 0; i <= n; i++) {
        V3 p = a + d * ((float)i / n);
        float groundY = world.groundSlow(p.x, p.z, p.y - 0.3f);
        SM.limb(V3(p.x, groundY, p.z), p, 0.05f, 0.05f, V3(1, 0, 0), col, MAT_METAL);
    }
    if (grind) addRailW(a, b, RK_METAL);
    // thin blocking solid under the rail so you cannot roll through it
    V3 m = (a + b) * 0.5f;
    float rot = std::atan2(d.x, d.z);
    int si = world.addBox(m.x, m.z, rot, 0.04f, len(V3(d.x, 0, d.z)) * 0.5f, std::min(a.y, b.y) - 0.7f, std::min(a.y, b.y) - 0.06f, SURF_METAL);
    world.solids[si].noGround = true;
}

// Plywood kicker ramp rising towards fwdYaw(rot)
static void kicker(float cx, float cz, float rot, float hx, float hz, float h, float y0 = 0.15f, Col col = C_PLYWOOD, uint8_t mat = MAT_WOOD, int surf = SURF_WOOD) {
    M4 F = frame(cx, y0, cz, rot);
    V3 p0 = xPoint(F, V3(-hx, 0, -hz)), p1 = xPoint(F, V3(hx, 0, -hz)), p2 = xPoint(F, V3(hx, h, hz)), p3 = xPoint(F, V3(-hx, h, hz));
    V3 b2 = xPoint(F, V3(hx, 0, hz)), b3 = xPoint(F, V3(-hx, 0, hz));
    V3 up = xDir(F, V3(0, 1, -h / (2 * hz)));
    SM.quadOut(p0, p1, p2, p3, up, col, mat);
    triOut(SM, p1, b2, p2, xDir(F, V3(1, 0, 0)), shade(col, 0.8f), mat);
    triOut(SM, p0, b3, p3, xDir(F, V3(-1, 0, 0)), shade(col, 0.8f), mat);
    SM.quadOut(b3, b2, p2, p3, xDir(F, V3(0, 0, 1)), shade(col, 0.7f), mat);
    // metal lip plate
    SM.limb(p3 + xDir(F, V3(0, 0.005f, -0.03f)), p2 + xDir(F, V3(0, 0.005f, -0.03f)), 0.05f, 0.02f, V3(0, 1, 0), C_STEEL, MAT_METAL);
    world.addRamp(cx, cz, rot, hx, hz, y0, h, surf);
}

// Quarter pipe; (cx,cz) is the coping midpoint, the ramp faces direction fwdYaw(rot+PI) (you ride towards fwdYaw(rot))
static void quarterPipe(float cx, float cz, float rot, float hx, float R, float H, float deck, float y0 = 0.15f,
                        Col col = C_PLYWOOD) {
    float W = std::sqrt(std::max(R * R - (R - H) * (R - H), 0.01f));
    V3 fz = fwdYaw(rot);
    float fcx = cx - fz.x * W * 0.5f, fcz = cz - fz.z * W * 0.5f;   // footprint center
    M4 F = frame(fcx, y0, fcz, rot);
    const int N = 12;
    for (int i = 0; i < N; i++) {
        float xa = W * i / N, xb = W * (i + 1) / N;
        float ya = R - std::sqrt(R * R - xa * xa), yb = R - std::sqrt(R * R - xb * xb);
        float za = -W * 0.5f + xa, zb = -W * 0.5f + xb;
        V3 q[4] = {xPoint(F, V3(-hx, ya, za)), xPoint(F, V3(hx, ya, za)), xPoint(F, V3(hx, yb, zb)), xPoint(F, V3(-hx, yb, zb))};
        float xm = (xa + xb) * 0.5f, k = xm / std::sqrt(R * R - xm * xm);
        SM.quadOut(q[0], q[1], q[2], q[3], xDir(F, V3(0, 1, -k)), col, MAT_WOOD);
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            V3 s0 = xPoint(F, V3(sgn * hx, 0, za)), s1 = xPoint(F, V3(sgn * hx, 0, zb));
            V3 t0 = xPoint(F, V3(sgn * hx, ya, za)), t1 = xPoint(F, V3(sgn * hx, yb, zb));
            SM.quadOut(s0, s1, t1, t0, xDir(F, V3((float)sgn, 0, 0)), shade(col, 0.75f), MAT_WOOD);
        }
    }
    // back wall + deck
    SM.quadOut(xPoint(F, V3(-hx, 0, W * 0.5f)), xPoint(F, V3(hx, 0, W * 0.5f)), xPoint(F, V3(hx, H, W * 0.5f)), xPoint(F, V3(-hx, H, W * 0.5f)),
               xDir(F, V3(0, 0, 1)), shade(col, 0.6f), MAT_WOOD);
    world.addQP(fcx, fcz, rot, hx, R, H, y0, SURF_WOOD);
    if (deck > 0.05f) {
        V3 dc = V3(cx, 0, cz) + fz * (deck * 0.5f);
        Col dcol = shade(col, 0.9f);
        solidBox(dc.x, dc.z, rot, hx, deck * 0.5f, y0, y0 + H, shade(col, 0.7f), MAT_WOOD, SURF_WOOD, false, &dcol);
    }
    // coping
    V3 ca = xPoint(F, V3(-hx, H, W * 0.5f)), cb = xPoint(F, V3(hx, H, W * 0.5f));
    SM.limb(ca, cb, 0.07f, 0.07f, V3(0, 1, 0), C_STEEL, MAT_METAL);
    addRailW(ca + V3(0, 0.02f, 0), cb + V3(0, 0.02f, 0), RK_METAL);
}

// Staircase rising towards fwdYaw(rot). Returns top height.
static float stairs(float cx, float cz, float rot, float hx, int n, float stepH, float stepD, float y0, Col col, uint8_t mat, bool rails) {
    float L = n * stepD;
    for (int i = 0; i < n; i++) {
        // each step is a box from its front edge to the top end of the flight
        float z0 = -L * 0.5f + i * stepD;
        float hz = (L * 0.5f - z0) * 0.5f;
        V3 center = V3(cx, 0, cz) + rotLocal(rot, 0, z0 + hz);
        solidBox(center.x, center.z, rot, hx, hz, y0, y0 + (i + 1) * stepH, col, mat, SURF_CONCRETE, false, nullptr, 63 & ~8);
    }
    if (rails) {
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            V3 a = V3(cx, 0, cz) + rotLocal(rot, sgn * (hx - 0.12f), -L * 0.5f + stepD * 0.5f);
            V3 b = V3(cx, 0, cz) + rotLocal(rot, sgn * (hx - 0.12f), L * 0.5f - stepD * 0.5f);
            a.y = y0 + stepH + 0.85f; b.y = y0 + n * stepH + 0.85f;
            handrail(a, b, true, C_IRON, 1.4f);
        }
    }
    return y0 + n * stepH;
}

// Low granite / concrete ledge with grindable top edges
static void ledge(float cx, float cz, float rot, float hx, float hz, float y0, float h, Col col = C_GRANITE, uint8_t mat = MAT_GRANITE) {
    Col top = shade(col, 1.08f);
    solidBox(cx, cz, rot, hx, hz, y0, y0 + h, col, mat, SURF_CONCRETE, false, &top);
    // steel edge protector
    V3 ax = rotLocal(rot, 1, 0), az = rotLocal(rot, 0, 1);
    bool xl = hx >= hz;
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        V3 c = V3(cx, y0 + h, cz) + (xl ? az * (sgn * hz) : ax * (sgn * hx));
        V3 e = xl ? ax * hx : az * hz;
        SM.limb(c - e, c + e, 0.035f, 0.035f, V3(0, 1, 0), hexc(0x6e6f70), MAT_METAL);
    }
    edgeRails(cx, cz, rot, hx, hz, y0 + h, RK_LEDGE);
}

static void tree(float x, float z, float y0, float scale = 1.f) {
    Rng r((uint32_t)(x * 131 + z * 71 + 999));
    SM.boxAA(V3(x - 0.75f, y0 - 0.02f, z - 0.75f), V3(x + 0.75f, y0 + 0.02f, z + 0.75f), hexc(0x4a3b2c), MAT_CONCRETE, 4);
    float th = 2.6f * scale;
    SM.cylinder(frame(x, y0, z, 0), 0.12f * scale, th, 7, hexc(0x4d3d30), MAT_PLAIN, false, 0.08f * scale);
    world.addBox(x, z, 0, 0.14f, 0.14f, y0, y0 + th + 2.f, SURF_WOOD, false);
    for (int k = 0; k < 4; k++) {
        V3 c(x + r.range(-0.8f, 0.8f) * scale, y0 + th + r.range(0.3f, 1.6f) * scale, z + r.range(-0.8f, 0.8f) * scale);
        float rad = r.range(1.0f, 1.5f) * scale;
        Col g = mixc(hexc(0x3f6b2a), hexc(0x6f8f35), r.f());
        SM.sphere(mTranslate(c), V3(rad, rad * 0.8f, rad), 8, 5, g, MAT_FOLIAGE);
    }
    // low iron tree-pit guard (grindable, NYC classic)
    float g = 0.8f, gy = y0 + 0.42f;
    V3 p[4] = {V3(x - g, gy, z - g), V3(x + g, gy, z - g), V3(x + g, gy, z + g), V3(x - g, gy, z + g)};
    for (int i = 0; i < 4; i++) {
        SM.limb(p[i], p[(i + 1) % 4], 0.035f, 0.035f, V3(0, 1, 0), C_IRON, MAT_METAL);
        SM.limb(V3(p[i].x, y0, p[i].z), p[i], 0.04f, 0.04f, V3(1, 0, 0), C_IRON, MAT_METAL);
        addRailW(p[i], p[(i + 1) % 4], RK_METAL);
    }
    world.addBox(x, z, 0, g, g, y0, gy - 0.05f, SURF_METAL, false);
}

static void streetLamp(float x, float z, float y0, float yaw) {
    SM.cylinder(frame(x, y0, z, 0), 0.11f, 0.6f, 8, hexc(0x2f3a33), MAT_METAL);
    SM.cylinder(frame(x, y0, z, 0), 0.07f, 6.6f, 8, hexc(0x3a463e), MAT_METAL, true, 0.05f);
    V3 top(x, y0 + 6.6f, z);
    V3 arm = top + fwdYaw(yaw) * 1.6f + V3(0, 0.25f, 0);
    SM.limb(top, arm, 0.07f, 0.07f, V3(0, 1, 0), hexc(0x3a463e), MAT_METAL);
    M4 hf = mTranslate(arm + V3(0, -0.05f, 0)) * mRotY(yaw);
    SM.box(hf, V3(0.22f, 0.08f, 0.45f), hexc(0x3a463e), MAT_METAL);
    SM.box(hf * mTranslate(V3(0, -0.09f, 0.05f)), V3(0.16f, 0.02f, 0.3f), hexc(0xfff1c0), MAT_EMISSIVE);
    world.addBox(x, z, 0, 0.12f, 0.12f, y0, y0 + 6.6f, SURF_METAL, false);
    lamps.push_back({arm + V3(0, -0.15f, 0)});
}

static void hydrant(float x, float z, float y0, bool open, float sprayYaw = 0) {
    Col red = hexc(0xb8261d);
    SM.cylinder(frame(x, y0, z, 0), 0.15f, 0.55f, 10, red, MAT_PAINTED);
    SM.sphere(mTranslate(V3(x, y0 + 0.58f, z)), V3(0.16f, 0.12f, 0.16f), 8, 4, red, MAT_PAINTED);
    SM.box(frame(x, y0 + 0.36f, z, sprayYaw), V3(0.26f, 0.06f, 0.06f), shade(red, 0.9f), MAT_PAINTED);
    SM.box(frame(x, y0 + 0.36f, z, sprayYaw + PI / 2), V3(0.06f, 0.06f, 0.22f), shade(red, 0.9f), MAT_PAINTED);
    world.addBox(x, z, 0, 0.2f, 0.2f, y0, y0 + 0.72f, SURF_METAL, false);
    world.addGap("HYDRANT HOP", 150, x, z, 0, 0.25f, 0.25f, y0 + 0.75f);
    if (open) {
        V3 d = fwdYaw(sprayYaw);
        emitters.push_back({V3(x, y0 + 0.36f, z) + d * 0.3f, d, EM_HYDRANT, 260.f});
    }
}

static void puddle(float x, float z, float y, float rx, float rz) {
    const int N = 14;
    uint32_t c0 = (uint32_t)WM.v.size();
    WM.vert(V3(x, y, z), V3(0, 1, 0), Col(0, 0, 255), 2);
    for (int i = 0; i < N; i++) {
        float a = TAU * i / N;
        float wob = 0.8f + 0.35f * hashf((int)(x * 10), (int)(z * 10), i);
        WM.vert(V3(x + std::sin(a) * rx * wob, y, z + std::cos(a) * rz * wob), V3(0, 1, 0), Col(0, 0, 0), 2);
    }
    for (int i = 0; i < N; i++) WM.idx.insert(WM.idx.end(), {c0, c0 + 1 + (uint32_t)i, c0 + 1 + (uint32_t)((i + 1) % N)});
}

static void manhole(float x, float z, bool steam) {
    SM.cylinder(frame(x, 0.0f, z, 0), 0.4f, 0.012f, 14, hexc(0x2b2a28), MAT_METAL);
    if (steam) emitters.push_back({V3(x, 0.05f, z), V3(0, 1, 0), EM_STEAM, 18.f});
}

// Car body geometry. type 0 = yellow cab, 1 = sedan, 2 = town car, 3 = van. F: ground frame, nose +Z.
static void carGeom(MeshBuilder& mb, const M4& F, int type, Col body, float brake = 0) {
    float L = type == 3 ? 2.6f : 2.45f, Wd = 0.95f;
    float bodyTop = type == 3 ? 1.9f : 0.95f;
    mb.box(F * mTranslate(V3(0, (0.3f + bodyTop) * 0.5f, 0)), V3(Wd, (bodyTop - 0.3f) * 0.5f, L), body, MAT_PAINTED);
    mb.box(F * mTranslate(V3(0, 0.36f, 0)), V3(Wd + 0.02f, 0.06f, L + 0.04f), hexc(0x2a2a2a), MAT_PLAIN);   // bumpers/rocker
    if (type != 3) {
        mb.box(F * mTranslate(V3(0, 1.22f, -0.15f)), V3(Wd * 0.9f, 0.27f, 1.15f), body, MAT_PAINTED);
        mb.box(F * mTranslate(V3(0, 1.2f, -0.15f)), V3(Wd * 0.92f, 0.2f, 1.1f), hexc(0x1d2630), MAT_PAINTED, 1 | 2 | 16 | 32);
    } else {
        mb.box(F * mTranslate(V3(0, 1.45f, 1.9f)), V3(Wd * 0.95f, 0.25f, 0.72f), hexc(0x1d2630), MAT_PAINTED, 16 | 1 | 2);
    }
    for (int i = 0; i < 4; i++) {
        float sx = (i & 1) ? Wd * 0.92f : -Wd * 0.92f, sz = (i & 2) ? L * 0.62f : -L * 0.62f;
        mb.box(F * mTranslate(V3(sx, 0.32f, sz)), V3(0.12f, 0.32f, 0.32f), hexc(0x111111), MAT_PLAIN);
    }
    mb.box(F * mTranslate(V3(0.6f, 0.75f, L + 0.005f)), V3(0.18f, 0.07f, 0.01f), hexc(0xfff4d0), MAT_EMISSIVE);
    mb.box(F * mTranslate(V3(-0.6f, 0.75f, L + 0.005f)), V3(0.18f, 0.07f, 0.01f), hexc(0xfff4d0), MAT_EMISSIVE);
    Col tail = brake > 0.5f ? hexc(0xff2a1a) : hexc(0x8a1010);
    mb.box(F * mTranslate(V3(0.65f, 0.78f, -L - 0.005f)), V3(0.15f, 0.07f, 0.01f), tail, MAT_EMISSIVE);
    mb.box(F * mTranslate(V3(-0.65f, 0.78f, -L - 0.005f)), V3(0.15f, 0.07f, 0.01f), tail, MAT_EMISSIVE);
    if (type == 0) {   // taxi roof light + checker stripe
        mb.box(F * mTranslate(V3(0, 1.56f, -0.1f)), V3(0.35f, 0.08f, 0.12f), hexc(0xfff6c8), MAT_EMISSIVE);
        mb.box(F * mTranslate(V3(Wd + 0.005f, 0.72f, 0)), V3(0.005f, 0.05f, L * 0.8f), hexc(0x111111), MAT_PLAIN);
        mb.box(F * mTranslate(V3(-Wd - 0.005f, 0.72f, 0)), V3(0.005f, 0.05f, L * 0.8f), hexc(0x111111), MAT_PLAIN);
    }
}

static void parkedCar(float x, float z, float yaw, int type, Col body) {
    float L = type == 3 ? 2.6f : 2.45f, Wd = 0.95f;
    float bodyTop = type == 3 ? 1.9f : 0.95f;
    carGeom(SM, frame(x, 0, z, yaw), type, body);
    world.addBox(x, z, yaw, Wd, L, 0, bodyTop, SURF_METAL, false);
    if (type != 3) world.addBox(x + rotLocal(yaw, 0, -0.15f).x, z + rotLocal(yaw, 0, -0.15f).z, yaw, Wd * 0.9f, 1.15f, 0, 1.49f, SURF_METAL, false);
    world.addGap(type == 0 ? "CAB HOP" : (type == 3 ? "VAN HOP" : "CAR HOP"), type == 3 ? 400 : 250, x, z, yaw, Wd, L, type == 3 ? 1.95f : 1.5f);
}

static void signBoard(V3 pos, V3 right, V3 up, V3 out, float w, float h, const std::string& txt, Col bg, Col fg, bool neon) {
    V3 c = pos;
    M4 F = mBasis(right, up, out, c);
    SM.box(F, V3(w * 0.5f, h * 0.5f, 0.06f), bg, MAT_PAINTED);
    float px = std::min(h * 0.62f / 7.f, (w * 0.9f) / (txt.size() * 6.f));
    float tw = textWidth3D(txt, px);
    V3 o = c - right * (tw * 0.5f) - up * (3.5f * px) + out * 0.065f;
    SM.text3D(txt, o, right, up, px, fg, neon ? MAT_EMISSIVE : MAT_PLAIN);
}

// Storefront on a building face. 'o' = left end at ground, 'r' = along facade, 'n' = outward normal.
static void storefront(V3 o, V3 r, V3 n, float w, const std::string& name, Col signBg, Col signFg, Col awning, bool hasAwning, Rng& rng) {
    V3 up(0, 1, 0);
    float sfH = 4.8f;
    // piers
    Col pier = hexc(0x5b5048);
    M4 P0 = mBasis(r, up, n, o + r * 0.25f + up * (sfH * 0.5f) + n * 0.12f);
    SM.box(P0, V3(0.25f, sfH * 0.5f, 0.12f), pier, MAT_CONCRETE);
    M4 P1 = mBasis(r, up, n, o + r * (w - 0.25f) + up * (sfH * 0.5f) + n * 0.12f);
    SM.box(P1, V3(0.25f, sfH * 0.5f, 0.12f), pier, MAT_CONCRETE);
    // kick plate + glass
    M4 K = mBasis(r, up, n, o + r * (w * 0.5f) + up * 0.3f + n * 0.08f);
    SM.box(K, V3(w * 0.5f - 0.5f, 0.3f, 0.08f), hexc(0x4a4540), MAT_METAL);
    float doorW = 1.1f, doorX = rng.chance(0.5f) ? 0.9f : w - 0.9f - doorW;
    V3 gl0 = o + r * 0.5f + up * 0.6f + n * 0.03f;
    V3 g0 = gl0, g1 = o + r * (w - 0.5f) + up * 0.6f + n * 0.03f;
    SM.quadN(g0, g1, g1 + up * 2.9f, g0 + up * 2.9f, n, Col(255, 255, 255), MAT_SHOPGLASS);
    // door
    V3 d0 = o + r * doorX + n * 0.05f;
    M4 D = mBasis(r, up, n, d0 + r * (doorW * 0.5f) + up * 1.2f);
    SM.box(D, V3(doorW * 0.5f, 1.2f, 0.03f), hexc(0x2c2a28), MAT_PAINTED);
    // transom band + sign
    M4 T = mBasis(r, up, n, o + r * (w * 0.5f) + up * 3.75f + n * 0.1f);
    SM.box(T, V3(w * 0.5f - 0.5f, 0.25f, 0.1f), hexc(0x3d3935), MAT_PAINTED);
    signBoard(o + r * (w * 0.5f) + up * 4.3f + n * 0.12f, r, up, n, w - 1.0f, 0.75f, name, signBg, signFg, rng.chance(0.4f));
    // neon "OPEN" in the window sometimes
    if (rng.chance(0.55f)) {
        float px = 0.045f;
        V3 no = o + r * (doorX < w * 0.5f ? w - 2.2f : 1.0f) + up * 2.4f + n * 0.05f;
        SM.text3D("OPEN", no, r, up, px, rng.chance(0.5f) ? hexc(0xff3060) : hexc(0x40c0ff), MAT_EMISSIVE);
    }
    if (hasAwning) {
        V3 a0 = o + r * 0.3f + up * 3.5f + n * 0.2f, a1 = o + r * (w - 0.3f) + up * 3.5f + n * 0.2f;
        V3 b0 = a0 + n * 1.4f - up * 0.7f, b1 = a1 + n * 1.4f - up * 0.7f;
        SM.quadOut(a0, a1, b1, b0, up + n, awning, MAT_AWNING);
        SM.quadOut(a0, a1, b1, b0, -(up + n), shade(awning, 0.6f), MAT_AWNING);
        SM.quadOut(b0, b1, b1 - up * 0.3f, b0 - up * 0.3f, n, awning, MAT_AWNING);
    }
}

// Rooftop water tower (wooden tank on steel legs) -- the NYC skyline signature
static void waterTower(float x, float y, float z, float s) {
    for (int i = 0; i < 4; i++) {
        float a = PI / 4 + i * PI / 2;
        V3 f(std::sin(a) * 1.0f * s, 0, std::cos(a) * 1.0f * s);
        SM.limb(V3(x, y, z) + f, V3(x, y + 2.2f * s, z) + f * 0.9f, 0.1f * s, 0.1f * s, V3(1, 0, 0), hexc(0x2a2a2a), MAT_METAL);
    }
    SM.cylinder(frame(x, y + 2.2f * s, z, 0), 1.35f * s, 2.8f * s, 12, hexc(0x6b5238), MAT_WOOD, true);
    SM.cylinder(frame(x, y + 5.0f * s, z, 0), 1.45f * s, 0.9f * s, 12, hexc(0x3b3029), MAT_ROOF, true, 0.05f);
    for (int k = 0; k < 3; k++) SM.cylinder(frame(x, y + (2.5f + k * 0.9f) * s, z, 0), 1.37f * s, 0.06f, 12, hexc(0x222222), MAT_METAL, false);
}

// Fire escape (black iron balconies + ladders) on a facade
static void fireEscape(V3 o, V3 r, V3 n, float w, int floors) {
    for (int f = 0; f < floors; f++) {
        float y = 4.8f + 3.3f * f + 0.2f;
        V3 b0 = o + V3(0, y, 0), b1 = b0 + r * w;
        SM.box(mBasis(r, V3(0, 1, 0), n, (b0 + b1) * 0.5f + n * 0.6f), V3(w * 0.5f, 0.03f, 0.6f), C_IRON, MAT_METAL);
        V3 t0 = b0 + n * 1.2f + V3(0, 0.9f, 0), t1 = b1 + n * 1.2f + V3(0, 0.9f, 0);
        SM.limb(t0, t1, 0.04f, 0.04f, V3(0, 1, 0), C_IRON, MAT_METAL);
        SM.limb(b0 + n * 1.2f, t0, 0.04f, 0.04f, V3(1, 0, 0), C_IRON, MAT_METAL);
        SM.limb(b1 + n * 1.2f, t1, 0.04f, 0.04f, V3(1, 0, 0), C_IRON, MAT_METAL);
        for (int k = 1; k < 8; k++) {
            V3 p = b0 + r * (w * k / 8.f) + n * 1.2f;
            SM.limb(p, p + V3(0, 0.9f, 0), 0.015f, 0.015f, V3(1, 0, 0), C_IRON, MAT_METAL);
        }
        if (f + 1 < floors) {   // diagonal stair to the next balcony
            V3 s0 = b0 + r * (w * 0.2f) + n * 0.6f, s1 = b0 + r * (w * 0.75f) + n * 0.6f + V3(0, 3.3f, 0);
            SM.limb(s0, s1, 0.5f, 0.05f, n, C_IRON, MAT_METAL);
        }
    }
}

// ----------------------------------------------------------------------------
// The level: one NYC intersection, four corners, a waterfront promenade.
//   Avenue (N-S) road x in [-9,9]; cross street (E-W) z in [-7,7];
//   Water St z in [-74,-62]; promenade z in [-88,-74]; river beyond.
// ----------------------------------------------------------------------------
static const float SH = 0.15f;   // sidewalk height

static void building(float x0, float z0, float x1, float z1, float h, int style, Col col, uint32_t seed, bool roofStuff = true) {
    uint8_t mat = style == 1 ? MAT_STONEWIN : (style == 2 ? MAT_GLASSWALL : MAT_WINDOWS);
    SM.boxAA(V3(x0, 0, z0), V3(x1, h, z1), col, mat, 1 | 2 | 16 | 32);
    SM.quadN(V3(x0, h, z1), V3(x1, h, z1), V3(x1, h, z0), V3(x0, h, z0), V3(0, 1, 0), hexc(0x55514c), MAT_ROOF);
    world.addBox((x0 + x1) * 0.5f, (z0 + z1) * 0.5f, 0, (x1 - x0) * 0.5f, (z1 - z0) * 0.5f, 0, h, SURF_CONCRETE, true);
    Rng r(seed);
    if (style != 2) {
        Col cor = style == 1 ? shade(col, 0.85f) : hexc(0x6d665c);
        SM.boxAA(V3(x0 - 0.35f, h - 0.15f, z0 - 0.35f), V3(x1 + 0.35f, h + 0.45f, z1 + 0.35f), cor, MAT_CONCRETE);
        SM.boxAA(V3(x0 - 0.12f, 4.7f, z0 - 0.12f), V3(x1 + 0.12f, 4.95f, z1 + 0.12f), cor, MAT_CONCRETE);   // storefront cornice line
    } else {
        SM.boxAA(V3(x0 + 1, h, z0 + 1), V3(x1 - 1, h + 3, z1 - 1), hexc(0x5b6066), MAT_CONCRETE);
    }
    if (roofStuff && style != 2) {
        float w = x1 - x0, d = z1 - z0;
        if (r.chance(0.6f) && w > 5 && d > 5) waterTower(x0 + w * r.range(0.3f, 0.7f), h + 0.45f, z0 + d * r.range(0.3f, 0.7f), r.range(0.8f, 1.1f));
        int n = r.irange(1, 3);
        for (int i = 0; i < n; i++) {
            float cx = x0 + w * r.range(0.2f, 0.8f), cz = z0 + d * r.range(0.2f, 0.8f);
            SM.boxAA(V3(cx - 0.8f, h + 0.45f, cz - 0.6f), V3(cx + 0.8f, h + 1.5f, cz + 0.6f), hexc(0x8a8a88), MAT_METAL);
        }
    }
}

static const char* SHOP_NAMES[] = {
    "DELI & GROCERY", "PIZZA", "BAGELS", "VIDEO RENTAL", "LAUNDROMAT", "RECORDS", "NAIL SALON", "CHECK CASHING",
    "99 CENT STORE", "CHINESE FOOD", "LIQUORS", "PHARMACY", "SNEAKERS", "DINER", "BARBER SHOP", "HARDWARE",
    "CAFE", "TATTOO", "INTERNET CAFE", "PAGERS & CELL", "DVD VHS GAMES", "SKATE SHOP", "BODEGA 24 HRS", "TAILOR",
    "FRIED CHICKEN", "PAWN SHOP", "OPTICAL", "SHOE REPAIR"};
static int shopIdx = 0;

// A row of buildings with storefronts. 'left' is the street-view left corner of the row at ground level,
// 'out' the outward (street-facing) normal. Text reads left-to-right for someone on the sidewalk.
static void facadeRow(V3 left, V3 out, float length, float depth, uint32_t seed, bool shops, float minH, float maxH, int styleMode) {
    V3 up(0, 1, 0), r = cross(up, out);
    Rng rng(seed);
    static const uint32_t bricks[] = {0x8e4a36, 0x7a3b2c, 0xa0664a, 0x9c7a5a, 0x6e4535, 0xb08560, 0x8a5a44, 0x5e3a2e};
    static const uint32_t stones[] = {0xc8bfae, 0xb5ad9f, 0xd6ccb8, 0x9e978b};
    static const uint32_t signBg[] = {0xb3201b, 0xf2c318, 0x1f6b3a, 0x1d3f8f, 0x151515, 0xf0efe8, 0x7d1d4a, 0xe06a12};
    static const uint32_t signFg[] = {0xffffff, 0x141414, 0xffffff, 0xffe040, 0xf2c318, 0xc0201b, 0xffffff, 0xffffff};
    static const uint32_t awn[] = {0x1f6b3a, 0xa31c1c, 0x1d3f8f, 0x6b1d2a, 0x2a2a2a, 0xc26a10};
    float x = 0;
    while (x < length - 0.5f) {
        float w = rng.range(7.f, 10.f);
        if (length - (x + w) < 5.f) w = length - x;
        V3 p0 = left + r * x, p1 = left + r * (x + w), back = out * -depth;
        V3 mn(std::min({p0.x, p1.x, p0.x + back.x, p1.x + back.x}), 0, std::min({p0.z, p1.z, p0.z + back.z, p1.z + back.z}));
        V3 mx(std::max({p0.x, p1.x, p0.x + back.x, p1.x + back.x}), 0, std::max({p0.z, p1.z, p0.z + back.z, p1.z + back.z}));
        int style = styleMode >= 0 ? styleMode : (rng.chance(0.72f) ? 0 : 1);
        Col col = style == 1 ? hexc(stones[rng.irange(0, 3)]) : hexc(bricks[rng.irange(0, 7)]);
        float h = 4.8f + 3.3f * rng.irange((int)((minH - 4.8f) / 3.3f), (int)((maxH - 4.8f) / 3.3f)) + 0.4f;
        building(mn.x, mn.z, mx.x, mx.z, h, style, col, rng.next());
        if (shops) {
            int k = rng.irange(0, 7);
            storefront(p0 + out * 0.001f, r, out, w, SHOP_NAMES[shopIdx++ % 28], hexc(signBg[k]), hexc(signFg[k]),
                       hexc(awn[rng.irange(0, 5)]), rng.chance(0.6f), rng);
        }
        if (style == 0 && h > 12 && rng.chance(0.5f)) fireEscape(p0 + r * (w * 0.2f) + out * 0.01f, r, out, w * 0.6f, (int)((h - 5.f) / 3.3f));
        x += w;
    }
}

static void stripeQuad(V3 a, V3 dirLong, V3 dirWide, float L, float W, Col c) {
    V3 p0 = a, p1 = a + dirWide * W, p2 = p1 + dirLong * L, p3 = a + dirLong * L;
    SM.quadOut(p0, p1, p2, p3, V3(0, 1, 0), c, MAT_PLAIN);
}

static void buildStreets() {
    V3 up(0, 1, 0);
    SM.quadN(V3(-600, 0, 600), V3(600, 0, 600), V3(600, 0, RIVER_EDGE_Z), V3(-600, 0, RIVER_EDGE_Z), up, C_ASPHALT, MAT_ASPHALT);
    // sidewalk slabs (curbs are grindable)
    slab(-200, -62, -9, -7, SH, C_SIDEWALK, MAT_SIDEWALK, SURF_SIDEWALK, 2 | 4 | 8);
    slab(9, -62, 200, -7, SH, C_SIDEWALK, MAT_SIDEWALK, SURF_SIDEWALK, 1 | 4 | 8);
    slab(-200, 7, -9, 200, SH, C_SIDEWALK, MAT_SIDEWALK, SURF_SIDEWALK, 2 | 4);
    slab(9, 7, 200, 200, SH, C_SIDEWALK, MAT_SIDEWALK, SURF_SIDEWALK, 1 | 4);
    slab(-600, RIVER_EDGE_Z, 600, -74, SH, C_SIDEWALK, MAT_SIDEWALK, SURF_SIDEWALK, 8);
    // road markings
    Col white = hexc(0xdedbd2), yel = hexc(0xe0b416);
    float y = 0.006f;
    for (float z = -60; z < 200; z += 6) {
        if (z > -12 && z < 12) continue;
        stripeQuad(V3(-2.3f, y, z), V3(0, 0, 1), V3(1, 0, 0), 3, 0.14f, white);
        stripeQuad(V3(2.2f, y, z), V3(0, 0, 1), V3(1, 0, 0), 3, 0.14f, white);
    }
    for (float x = -200; x < 200; x += 6) {
        if (x > -14 && x < 14) continue;
        stripeQuad(V3(x, y, -0.07f), V3(1, 0, 0), V3(0, 0, 1), 3, 0.14f, white);
    }
    stripeQuad(V3(-600, y, -68.2f), V3(1, 0, 0), V3(0, 0, 1), 1200, 0.12f, yel);
    stripeQuad(V3(-600, y, -67.9f), V3(1, 0, 0), V3(0, 0, 1), 1200, 0.12f, yel);
    // crosswalks
    for (int side = -1; side <= 1; side += 2) {
        for (float x = -8.5f; x < 8.5f; x += 1.1f) stripeQuad(V3(x, y, side > 0 ? 8.0f : -11.0f), V3(0, 0, 1), V3(1, 0, 0), 3.0f, 0.55f, white);
        for (float z = -6.5f; z < 6.5f; z += 1.1f) stripeQuad(V3(side > 0 ? 10.0f : -13.0f, y, z), V3(1, 0, 0), V3(0, 0, 1), 3.0f, 0.55f, white);
        stripeQuad(V3(-8.8f, y, side > 0 ? 11.4f : -11.8f), V3(1, 0, 0), V3(0, 0, 1), 17.6f, 0.4f, white);
    }
    for (float z = -73.5f; z < -62.5f; z += 1.1f) {
        stripeQuad(V3(-13.0f, y, z), V3(1, 0, 0), V3(0, 0, 1), 3.0f, 0.55f, white);
        stripeQuad(V3(10.0f, y, z), V3(1, 0, 0), V3(0, 0, 1), 3.0f, 0.55f, white);
    }
    for (float x = -8.5f; x < 8.5f; x += 1.1f) stripeQuad(V3(x, y, -61.0f), V3(0, 0, 1), V3(1, 0, 0), 3.0f, 0.55f, white);
    // manholes, steam, puddles
    manhole(-3.5f, -30, true); manhole(4, 36, true); manhole(-30, 1.5f, false); manhole(38, -2, true); manhole(-3, -69, false);
    puddle(-8.2f, 20, 0.012f, 1.6f, 3.2f);
    puddle(5, -45, 0.012f, 2.2f, 1.4f);
    puddle(26, 5.5f, 0.012f, 2.6f, 1.1f);
    puddle(-45, -66, 0.012f, 1.8f, 2.8f);
    // traffic lights at the corners
    float tl[4][2] = {{-10.2f, -8.2f}, {10.2f, -8.2f}, {-10.2f, 8.2f}, {10.2f, 8.2f}};
    for (int i = 0; i < 4; i++) {
        float x = tl[i][0], z = tl[i][1];
        SM.cylinder(frame(x, SH, z, 0), 0.1f, 4.2f, 8, hexc(0x2e3530), MAT_METAL);
        world.addBox(x, z, 0, 0.12f, 0.12f, SH, 4.4f, SURF_METAL);
        float yawEW = x < 0 ? -PI / 2 : PI / 2;      // faces traffic on the cross street
        float yawNS = z < 0 ? PI : 0;
        tlights.push_back({V3(x, SH + 3.5f, z), yawEW, 0});
        tlights.push_back({V3(x, SH + 3.5f, z), yawNS, 1});
    }
}

static void subwayEntrance(float cx, float cz) {
    float hx = 1.1f, hz = 2.3f, ry = SH + 1.0f;
    V3 c[4] = {V3(cx - hx, ry, cz - hz), V3(cx + hx, ry, cz - hz), V3(cx + hx, ry, cz + hz), V3(cx - hx, ry, cz + hz)};
    for (int i = 0; i < 4; i++) handrail(c[i], c[(i + 1) % 4], true, hexc(0x2c4a36), 1.2f);
    for (int i = 0; i < 4; i++) {   // side panels
        V3 a = c[i], b = c[(i + 1) % 4];
        V3 d = norm(b - a), n = cross(V3(0, 1, 0), d);
        SM.quadOut(V3(a.x, SH + 0.1f, a.z), V3(b.x, SH + 0.1f, b.z), b - V3(0, 0.08f, 0), a - V3(0, 0.08f, 0), -n, hexc(0x24402f), MAT_METAL);
        SM.quadOut(V3(a.x, SH + 0.1f, a.z), V3(b.x, SH + 0.1f, b.z), b - V3(0, 0.08f, 0), a - V3(0, 0.08f, 0), n, hexc(0x24402f), MAT_METAL);
    }
    SM.boxAA(V3(cx - hx, SH, cz - hz), V3(cx + hx, SH + 0.03f, cz + hz), hexc(0x55585a), MAT_METAL);   // closed steel plate
    signBoard(V3(cx + hx + 0.07f, SH + 0.75f, cz), V3(0, 0, -1), V3(0, 1, 0), V3(1, 0, 0), 3.4f, 0.32f, "SUBWAY", hexc(0x151515), C_WHITE, false);
    for (int s = -1; s <= 1; s += 2) {
        V3 p(cx + hx, SH, cz + s * hz);
        SM.cylinder(frame(p.x, p.y, p.z, 0), 0.05f, 1.9f, 6, hexc(0x2c4a36), MAT_METAL);
        SM.sphere(mTranslate(p + V3(0, 2.05f, 0)), V3(0.17f, 0.17f, 0.17f), 8, 5, hexc(0x40ff70), MAT_EMISSIVE);
    }
}

static void buildNW() {
    V3 up(0, 1, 0);
    // avenue face (x=-14, faces +X), cross-street face (z=-12, faces +Z), Water St face (z=-57, faces -Z)
    facadeRow(V3(-14, 0, -12), V3(1, 0, 0), 45, 16, 11, true, 12, 30, -1);
    facadeRow(V3(-69.5f, 0, -12), V3(0, 0, 1), 55.5f, 16, 12, true, 12, 26, -1);
    facadeRow(V3(-14, 0, -57), V3(0, 0, -1), 55.5f, 16, 13, true, 12, 24, -1);
    building(-200, -57, -69.5f, -12, 22, 0, hexc(0x7a4a3a), 14);
    building(-70, -45, -26, -24, 14, 0, hexc(0x6a3a2e), 15, false);   // block interior filler
    subwayEntrance(-11.4f, -30.5f);
    tree(-10.3f, -45, SH); tree(-10.3f, -18, SH); tree(-30, -8.1f, SH); tree(-52, -8.1f, SH);
    for (float z = -55; z < -10; z += 22) streetLamp(-9.7f, z, SH, PI / 2);
    streetLamp(-40, -7.6f, SH, 0);
    streetLamp(-35, -59.3f, SH, 0);
    // corner props: newspaper boxes, mailbox, trash
    Col news[4] = {hexc(0xc21f1f), hexc(0x1d4f9f), hexc(0xf2c318), hexc(0xe8e8e0)};
    for (int i = 0; i < 4; i++) {
        float z = -24.5f + i * 0.62f;
        Col top = shade(news[i], 0.9f);
        solidBox(-9.9f, z, 0, 0.25f, 0.28f, SH, SH + 1.05f, news[i], MAT_PAINTED, SURF_METAL, false, &top);
    }
    edgeRails(-9.9f, -23.57f, 0, 0.25f, 1.24f, SH + 1.05f, RK_METAL);
    solidBox(-9.9f, -40, 0, 0.33f, 0.38f, SH, SH + 1.2f, hexc(0x2a4c9a), MAT_PAINTED, SURF_METAL);   // mailbox
    SM.text3D("US MAIL", V3(-9.56f, SH + 0.85f, -39.67f), V3(0, 0, -1), V3(0, 1, 0), 0.016f, C_WHITE, MAT_PLAIN);
    // payphones
    for (int i = 0; i < 2; i++) {
        float z = -50.5f + i * 1.1f;
        SM.cylinder(frame(-10.0f, SH, z, 0), 0.06f, 1.4f, 6, C_STEEL, MAT_METAL);
        solidBox(-10.0f, z, 0, 0.3f, 0.45f, SH + 1.35f, SH + 2.3f, hexc(0x7c8a96), MAT_METAL, SURF_METAL);
        SM.boxAA(V3(-9.72f, SH + 1.5f, z - 0.3f), V3(-9.69f, SH + 2.1f, z + 0.3f), hexc(0x1b1b1b), MAT_PAINTED);
        world.addBox(-10.0f, z, 0, 0.1f, 0.1f, SH, SH + 1.35f, SURF_METAL);
    }
    pigeonSpots.push_back(V3(-12, SH, -8.5f));
    pigeonSpots.push_back(V3(-40, SH, -59.5f));
    npcPaths.push_back({{V3(-12.9f, SH, -60.2f), V3(-12.9f, SH, -10.2f), V3(-68, SH, -10.2f)}, false});
    npcPaths.push_back({{V3(-68, SH, -59.8f), V3(-11.5f, SH, -59.8f), V3(-11.5f, SH, -77), V3(-45, SH, -77)}, false});
    npcPaths.push_back({{V3(-12.9f, SH, -40), V3(-12.9f, SH, -10.2f), V3(11.0f, SH, -10.0f), V3(11.0f, SH, -45)}, false});
}

static void buildFountain(float cx, float cz) {
    const int N = 24;
    float rOut = 5.0f, rIn = 4.5f, top = SH + 0.5f;
    Col rim = hexc(0xb9b2a4);
    for (int i = 0; i < N; i++) {
        float a0 = TAU * i / N, a1 = TAU * (i + 1) / N, am = (a0 + a1) * 0.5f;
        float rm = (rOut + rIn) * 0.5f;
        float segL = 2 * rm * std::sin(PI / N) + 0.06f;
        V3 c = V3(cx, 0, cz) + V3(std::sin(am), 0, std::cos(am)) * rm;
        Col t = shade(rim, 1.1f);
        solidBox(c.x, c.z, am, segL * 0.5f, (rOut - rIn) * 0.5f, SH, top, rim, MAT_GRANITE, SURF_CONCRETE, false, &t);
        V3 p0 = V3(cx + std::sin(a0) * rm, top, cz + std::cos(a0) * rm), p1 = V3(cx + std::sin(a1) * rm, top, cz + std::cos(a1) * rm);
        addRailW(p0, p1, RK_LEDGE);
    }
    // basin floor + water surface (pool center is encoded in the vertex colour for the ripple shader)
    SM.cylinder(frame(cx, SH, cz, 0), rIn, 0.02f, N, hexc(0x3e6f78), MAT_CONCRETE);
    Col enc(0, 0, 255);
    uint32_t c0 = (uint32_t)WM.v.size();
    WM.vert(V3(cx, top - 0.12f, cz), V3(0, 1, 0), enc, 1);
    for (int i = 0; i < N; i++) {
        float a = TAU * i / N;
        WM.vert(V3(cx + std::sin(a) * rIn, top - 0.12f, cz + std::cos(a) * rIn), V3(0, 1, 0), enc, 1);
    }
    for (int i = 0; i < N; i++) WM.idx.insert(WM.idx.end(), {c0, c0 + 1 + (uint32_t)i, c0 + 1 + (uint32_t)((i + 1) % N)});
    world.pools.push_back({cx, cz, rIn, SH + 0.05f});
    // centre sculpture: pedestal, bowl, spout
    Col stone = hexc(0xc9c1b0);
    SM.cylinder(frame(cx, SH, cz, 0), 0.8f, 1.1f, 12, stone, MAT_GRANITE);
    SM.cylinder(frame(cx, SH + 1.1f, cz, 0), 0.6f, 0.25f, 12, stone, MAT_GRANITE, true, 1.9f);
    SM.cylinder(frame(cx, SH + 1.35f, cz, 0), 1.9f, 0.12f, 12, stone, MAT_GRANITE);
    SM.cylinder(frame(cx, SH + 1.47f, cz, 0), 0.25f, 0.9f, 8, stone, MAT_GRANITE, true, 0.12f);
    world.addBox(cx, cz, 0, 0.8f, 0.8f, SH, SH + 1.1f, SURF_CONCRETE);
    world.addBox(cx, cz, 0, 1.3f, 1.3f, SH + 1.1f, SH + 1.47f, SURF_CONCRETE);
    emitters.push_back({V3(cx, SH + 2.35f, cz), V3(0, 1, 0), EM_FOUNTAIN, 240.f});
    emitters.push_back({V3(cx, SH + 1.5f, cz), V3(0, 1, 0), EM_POOLSPLASH, 90.f});
    world.addGap("FOUNTAIN GAP", 1000, cx, cz, 0, rIn * 0.8f, rIn * 0.8f, top + 0.3f);
}

static void buildPlaza() {
    // pavers + glass office tower on the east with a raised terrace, stairs and brick banks
    overlay(14, -57, 58, -12, SH + 0.003f, hexc(0x9a5b47), MAT_PAVERS);
    building(58, -57, 200, -12, 96, 2, hexc(0x8899aa), 21);
    float tTop = SH + 1.05f;
    Col gran = hexc(0x9a968f), gtop = hexc(0xa8a49c);
    solidBox(52, -34.5f, 0, 6, 17.5f, SH, tTop, gran, MAT_GRANITE, SURF_CONCRETE, false, &gtop);
    edgeRails(52, -34.5f, 0, 6, 17.5f, tTop, RK_LEDGE, false);
    stairs(44.8f, -34.5f, PI / 2, 3.5f, 5, 0.21f, 0.48f, SH, hexc(0xa39e95), MAT_GRANITE, true);
    world.addGap("TERRACE STAIRS", 300, 44.8f, -34.5f, PI / 2, 3.3f, 1.2f, tTop + 0.05f);
    // brick banks either side of the stairs (Brooklyn Banks style)
    for (int s = -1; s <= 1; s += 2) {
        float cz = -34.5f + s * 10.25f;
        kicker(44, cz, PI / 2, 6.75f, 2, 1.05f, SH, hexc(0xa0503a), MAT_BRICKBANK, SURF_BRICK);
    }
    buildFountain(28.0f + 0.0f, -34.5f);
    ledge(19, -21, 0, 3, 0.45f, SH, 0.5f);
    ledge(37, -21, 0, 3, 0.45f, SH, 0.5f);
    ledge(19, -48, 0, 3, 0.45f, SH, 0.5f);
    ledge(37, -48, 0, 3, 0.45f, SH, 0.5f);
    ledge(39.5f, -34.5f, 0, 1.4f, 2.6f, SH, 0.22f, hexc(0xb0aba2), MAT_CONCRETE);   // manny pad
    kicker(19.6f, -34.5f, PI / 2, 1.1f, 1.2f, 1.1f);                               // launch at the fountain
    // terrace planters and granite benches
    for (int i = 0; i < 3; i++) {
        float z = -48 + i * 13.5f;
        if (i == 1) continue;
        Col pt = hexc(0x5a4632);
        solidBox(54, z, 0, 1.4f, 1.4f, tTop, tTop + 0.55f, hexc(0x8f8a82), MAT_GRANITE, SURF_CONCRETE, false, &pt);
        edgeRails(54, z, 0, 1.4f, 1.4f, tTop + 0.55f, RK_LEDGE, false);
        tree(54, z, tTop + 0.55f, 0.8f);
    }
    ledge(50, -34.5f, 0, 0.5f, 2.5f, tTop, 0.45f);
    // the red cube sculpture (balanced on a corner)
    M4 cube = mTranslate(V3(22, SH + 2.3f, -54)) * mRotY(0.5f) * mRotAxis(norm(V3(1, 0, 1)), 0.9553f);
    SM.box(cube, V3(1.35f, 1.35f, 1.35f), hexc(0xc01e1e), MAT_PAINTED);
    SM.cylinder(frame(22, SH, -54, 0), 0.25f, 0.5f, 8, hexc(0x444444), MAT_METAL);
    world.addBox(22, -54, 0.5f, 1.2f, 1.2f, SH, SH + 4.5f, SURF_METAL);
    // hot dog cart + umbrella
    {
        float x = 12.6f, z = -15.5f;
        solidBox(x, z, 0, 0.55f, 1.0f, SH + 0.35f, SH + 1.0f, hexc(0xd8d8d0), MAT_METAL, SURF_METAL);
        SM.cylinder(frame(x, SH + 1.0f, z, 0), 0.03f, 1.3f, 6, C_STEEL, MAT_METAL);
        SM.cylinder(frame(x, SH + 2.3f, z, 0), 1.2f, 0.35f, 10, hexc(0xf2c318), MAT_CLOTH, true, 0.05f);
        SM.box(frame(x, SH + 0.18f, z + 0.7f, 0), V3(0.05f, 0.18f, 0.18f), hexc(0x222222), MAT_PLAIN);
        SM.box(frame(x, SH + 0.18f, z - 0.7f, 0), V3(0.05f, 0.18f, 0.18f), hexc(0x222222), MAT_PLAIN);
        signBoard(V3(x - 0.56f, SH + 0.72f, z), V3(0, 0, 1), V3(0, 1, 0), V3(-1, 0, 0), 1.8f, 0.4f, "HOT DOGS", hexc(0x1d3f8f), hexc(0xffe040), false);
    }
    // trees + lamps + benches along the avenue sidewalk
    tree(12.6f, -52, SH); tree(12.6f, -27, SH);
    for (float z = -55; z < -10; z += 22) streetLamp(9.7f, z, SH, -PI / 2);
    streetLamp(35, -7.6f, SH, 0);
    for (int i = 0; i < 2; i++) {
        float z = -42.5f + i * 1.0f;
        SM.cylinder(frame(9.8f, SH, z, 0), 0.06f, 1.4f, 6, C_STEEL, MAT_METAL);
        solidBox(9.8f, z, 0, 0.3f, 0.45f, SH + 1.35f, SH + 2.3f, hexc(0x7c8a96), MAT_METAL, SURF_METAL);
        world.addBox(9.8f, z, 0, 0.1f, 0.1f, SH, SH + 1.35f, SURF_METAL);
    }
    hydrant(9.9f, -32, SH, false);
    pigeonSpots.push_back(V3(24, SH, -27));
    pigeonSpots.push_back(V3(33, SH, -43));
    pigeonSpots.push_back(V3(48, tTop, -40));
    // pedestrians: sidewalks + a loop around the fountain
    npcPaths.push_back({{V3(10.8f, SH, -60.5f), V3(10.8f, SH, -10.0f), V3(56, SH, -10.0f)}, false});
    npcPaths.push_back({{V3(11.2f, SH, -59.6f), V3(56, SH, -59.6f)}, false});
    npcPaths.push_back({{V3(14, SH, -26), V3(41.5f, SH, -26), V3(41.5f, SH, -43), V3(14, SH, -43)}, true});
    npcPaths.push_back({{V3(56, tTop, -50), V3(56, tTop, -19)}, false});
}

// chain-link fence panel run between two points (with posts); blocks the skater
static void chainFence(V3 a, V3 b, float h) {
    V3 d = b - a; float L = lenXZ(d);
    V3 dir = d / std::max(L, 1e-4f), n = cross(V3(0, 1, 0), dir);
    V3 A = a, B = b, Bt = b + V3(0, h, 0), At = a + V3(0, h, 0);
    SM.quadOut(A, B, Bt, At, n, hexc(0x9ea4a8), MAT_FENCE);
    int posts = std::max(1, (int)(L / 3.0f));
    for (int i = 0; i <= posts; i++) {
        V3 p = a + d * ((float)i / posts);
        SM.cylinder(frame(p.x, p.y, p.z, 0), 0.045f, h + 0.05f, 6, hexc(0x8a9094), MAT_METAL);
    }
    SM.limb(At, Bt, 0.05f, 0.05f, V3(0, 1, 0), hexc(0x8a9094), MAT_METAL);
    V3 m = (a + b) * 0.5f;
    world.addBox(m.x, m.z, std::atan2(dir.x, dir.z), 0.06f, L * 0.5f, a.y - 0.3f, a.y + h, SURF_METAL, false);
}

static void hoop(float x, float z, float faceYaw) {
    V3 f = fwdYaw(faceYaw);
    V3 base(x, SH, z);
    SM.cylinder(frame(x, SH, z, 0), 0.09f, 3.3f, 8, hexc(0x3c4a5a), MAT_PAINTED);
    V3 top = base + V3(0, 3.3f, 0), bb = top + f * 1.0f + V3(0, 0.1f, 0);
    SM.limb(top, bb, 0.08f, 0.08f, V3(0, 1, 0), hexc(0x3c4a5a), MAT_PAINTED);
    M4 B = mTranslate(bb + V3(0, 0.25f, 0)) * mRotY(faceYaw);
    SM.box(B, V3(0.9f, 0.55f, 0.03f), hexc(0xe8e8e8), MAT_PAINTED);
    SM.box(B * mTranslate(V3(0, -0.1f, 0.035f)), V3(0.3f, 0.2f, 0.005f), hexc(0xc03020), MAT_PLAIN, 16);
    for (int i = 0; i < 10; i++) {
        float a0 = TAU * i / 10, a1 = TAU * (i + 1) / 10;
        V3 c = bb + f * 0.35f + V3(0, -0.2f, 0);
        SM.limb(c + V3(std::sin(a0), 0, std::cos(a0)) * 0.23f, c + V3(std::sin(a1), 0, std::cos(a1)) * 0.23f, 0.02f, 0.02f, V3(0, 1, 0), hexc(0xe0501a), MAT_METAL);
    }
    world.addBox(x, z, 0, 0.12f, 0.12f, SH, SH + 3.4f, SURF_METAL);
}

static void buildCourt() {
    float x0 = -48, x1 = -18, z0 = 16, z1 = 46;
    overlay(x0, z0, x1, z1, SH + 0.003f, hexc(0x3c6b4c), MAT_COURT);
    overlay(x0 + 1.5f, z0 + 1.5f, x1 - 1.5f, z1 - 1.5f, SH + 0.005f, hexc(0x9a4a3a), MAT_COURT);
    Col line = hexc(0xeeeeea);
    auto L = [&](float ax, float az, float bx, float bz) {
        V3 a(ax, SH + 0.008f, az), b(bx, SH + 0.008f, bz);
        V3 d = norm(b - a), n = cross(V3(0, 1, 0), d) * 0.05f;
        SM.quadOut(a - n, b - n, b + n, a + n, V3(0, 1, 0), line, MAT_PLAIN);
    };
    L(x0 + 1.5f, z0 + 1.5f, x1 - 1.5f, z0 + 1.5f); L(x1 - 1.5f, z0 + 1.5f, x1 - 1.5f, z1 - 1.5f);
    L(x1 - 1.5f, z1 - 1.5f, x0 + 1.5f, z1 - 1.5f); L(x0 + 1.5f, z1 - 1.5f, x0 + 1.5f, z0 + 1.5f);
    L(x0 + 1.5f, 31, x1 - 1.5f, 31);
    for (int i = 0; i < 16; i++) {
        float a0 = TAU * i / 16, a1 = TAU * (i + 1) / 16;
        L(-33 + std::sin(a0) * 1.8f, 31 + std::cos(a0) * 1.8f, -33 + std::sin(a1) * 1.8f, 31 + std::cos(a1) * 1.8f);
    }
    L(-35.5f, 17.5f, -35.5f, 23.3f); L(-30.5f, 17.5f, -30.5f, 23.3f); L(-35.5f, 23.3f, -30.5f, 23.3f);
    // fence with two openings
    float fy = SH, fh = 4.0f;
    chainFence(V3(x0 - 0.5f, fy, z0 - 0.5f), V3(-36, fy, z0 - 0.5f), fh);
    chainFence(V3(-30, fy, z0 - 0.5f), V3(x1 + 0.5f, fy, z0 - 0.5f), fh);
    chainFence(V3(x1 + 0.5f, fy, z0 - 0.5f), V3(x1 + 0.5f, fy, 27.5f), fh);
    chainFence(V3(x1 + 0.5f, fy, 33.5f), V3(x1 + 0.5f, fy, z1 + 0.5f), fh);
    chainFence(V3(x1 + 0.5f, fy, z1 + 0.5f), V3(x0 - 0.5f, fy, z1 + 0.5f), fh);
    chainFence(V3(x0 - 0.5f, fy, z1 + 0.5f), V3(x0 - 0.5f, fy, z0 - 0.5f), fh);
    signBoard(V3(-33, SH + 3.2f, z0 - 0.62f), V3(-1, 0, 0), V3(0, 1, 0), V3(0, 0, -1), 5.4f, 0.55f, "THE CAGE", hexc(0x1f6b3a), C_WHITE, false);
    hoop(-33, 17.0f, 0);
    // skater corner of the court: quarter pipe, funbox, flat bar, kicker
    quarterPipe(-33, 44.9f, 0, 5.0f, 2.6f, 1.9f, 1.05f);
    {
        float cx = -33, cz = 34.5f, top = SH + 0.6f;
        Col t = hexc(0xc9a36e);
        solidBox(cx, cz, 0, 1.6f, 1.3f, SH, top, hexc(0x9a7a50), MAT_WOOD, SURF_WOOD, false, &t);
        edgeRails(cx, cz, 0, 1.6f, 1.3f, top, RK_WOOD, false);
        kicker(cx, cz - 1.3f - 0.9f, 0, 1.6f, 0.9f, 0.6f);
        kicker(cx, cz + 1.3f + 0.9f, PI, 1.6f, 0.9f, 0.6f);
        world.addGap("FUNBOX GAP", 250, cx, cz, 0, 1.6f, 1.3f, top + 0.15f);
    }
    {   // flat bar on legs
        V3 a(-23.5f, SH + 0.38f, 22), b(-23.5f, SH + 0.38f, 36);
        SM.limb(a, b, 0.07f, 0.07f, V3(0, 1, 0), hexc(0xd0d2d4), MAT_METAL);
        for (float t = 0; t <= 1.01f; t += 0.5f) {
            V3 p = lerp3(a, b, t);
            SM.limb(V3(p.x - 0.25f, SH, p.z), p, 0.05f, 0.05f, V3(0, 0, 1), hexc(0xb0b2b4), MAT_METAL);
            SM.limb(V3(p.x + 0.25f, SH, p.z), p, 0.05f, 0.05f, V3(0, 0, 1), hexc(0xb0b2b4), MAT_METAL);
        }
        addRailW(a, b, RK_METAL);
        int si = world.addBox(-23.5f, 29, 0, 0.05f, 7, SH, SH + 0.33f, SURF_METAL);
        world.solids[si].noGround = true;
    }
    kicker(-42.5f, 24.5f, PI / 2, 1.2f, 1.0f, 0.55f);
    // courtside benches
    for (int i = 0; i < 2; i++) {
        float z = 22 + i * 16;
        Col t = hexc(0x7a5a3a);
        solidBox(-46.8f, z, 0, 0.3f, 1.4f, SH, SH + 0.46f, hexc(0x4a4a4a), MAT_WOOD, SURF_WOOD, false, &t);
        edgeRails(-46.8f, z, 0, 0.3f, 1.4f, SH + 0.46f, RK_WOOD);
    }
    letterPos.push_back(V3(-33, SH + 2.6f, 34.5f));   // 'A' over the funbox
    pigeonSpots.push_back(V3(-15.5f, SH, 20));
}

static void buildSW() {
    facadeRow(V3(-52, 0, 12), V3(0, 0, -1), 17.5f, 16, 31, true, 12, 24, -1);
    facadeRow(V3(-14, 0, 90), V3(1, 0, 0), 40, 16, 32, true, 12, 28, -1);
    building(-200, 12, -69.5f, 200, 20, 0, hexc(0x7d4b3b), 33);
    building(-69.5f, 28, -52, 200, 18, 1, hexc(0xbfb6a4), 34);
    building(-52, 50, -30, 200, 16, 0, hexc(0x6f4638), 35, false);
    building(-30, 90, -14, 200, 24, 0, hexc(0x8a5a44), 36);
    hydrant(-9.9f, 20, SH, true, PI / 2);          // open hydrant spraying across the avenue
    hydrant(-30, 7.6f, SH, false);
    tree(-10.3f, 32, SH); tree(-10.3f, 58, SH); tree(-56, 8.1f, SH);
    for (float z = 25; z < 70; z += 24) streetLamp(-9.7f, z, SH, PI / 2);
    streetLamp(-42, 7.6f, SH, PI);
    // bus stop shelter on the cross street
    {
        float cx = -24, cz = 8.9f;
        Col fr = hexc(0x4a5560);
        for (int i = 0; i < 4; i++) {
            float x = cx + ((i & 1) ? 2.0f : -2.0f), z = cz + ((i & 2) ? 0.7f : -0.7f);
            SM.cylinder(frame(x, SH, z, 0), 0.05f, 2.5f, 6, fr, MAT_METAL);
            world.addBox(x, z, 0, 0.07f, 0.07f, SH, SH + 2.5f, SURF_METAL);
        }
        SM.boxAA(V3(cx - 2.1f, SH + 2.5f, cz - 0.8f), V3(cx + 2.1f, SH + 2.62f, cz + 0.8f), fr, MAT_METAL);
        SM.quadOut(V3(cx - 2, SH + 0.3f, cz + 0.7f), V3(cx + 2, SH + 0.3f, cz + 0.7f), V3(cx + 2, SH + 2.4f, cz + 0.7f), V3(cx - 2, SH + 2.4f, cz + 0.7f), V3(0, 0, -1), hexc(0x9fb4c0), MAT_SHOPGLASS);
        Col t = hexc(0x5b6570);
        solidBox(cx, cz + 0.4f, 0, 1.5f, 0.2f, SH, SH + 0.48f, fr, MAT_METAL, SURF_METAL, false, &t);
        edgeRails(cx, cz + 0.4f, 0, 1.5f, 0.2f, SH + 0.48f, RK_METAL);
        signBoard(V3(cx + 2.0f, SH + 2.0f, cz - 0.72f), V3(-1, 0, 0), V3(0, 1, 0), V3(0, 0, -1), 0.9f, 0.3f, "BUS", hexc(0x1d3f8f), C_WHITE, false);
    }
    npcPaths.push_back({{V3(-12.8f, SH, 10.2f), V3(-12.8f, SH, 68)}, false});
    npcPaths.push_back({{V3(-68, SH, 10.4f), V3(-12.8f, SH, 10.4f), V3(-12.8f, SH, 9.0f), V3(11.0f, SH, 9.6f), V3(11.0f, SH, 40)}, false});
}

static void brownstone(float z0, int i) {
    static const uint32_t cols[] = {0x5e4032, 0x6b4a3a, 0x563a2d, 0x70503e};
    float z1 = z0 + 6.f;
    Col col = hexc(cols[i % 4]);
    building(16, z0, 30, z1, 4.8f + 3.6f * 3 + 0.3f, 1, col, 600 + i);
    float sz = z0 + 1.6f + (i % 2) * 2.8f;   // stoop position along the facade
    float top = stairs(14.0f, sz, PI / 2, 0.95f, 8, 0.2f, 0.4f, SH, shade(col, 1.05f), MAT_CONCRETE, false);
    Col lt = shade(col, 1.1f);
    solidBox(15.8f, sz, 0, 0.2f, 0.95f, SH, top, col, MAT_CONCRETE, SURF_CONCRETE, false, &lt);
    SM.boxAA(V3(15.95f, top, sz - 0.6f), V3(16.02f, top + 2.3f, sz + 0.6f), hexc(0x3a2418), MAT_WOOD);   // door
    SM.boxAA(V3(15.9f, top + 2.3f, sz - 0.75f), V3(16.05f, top + 2.9f, sz + 0.75f), hexc(0xe0c890), MAT_EMISSIVE);
    for (int s = -1; s <= 1; s += 2) {   // stoop handrails (sloped, grindable)
        V3 a(12.45f, SH + 0.2f + 0.85f, sz + s * 0.88f), b(15.55f, top + 0.85f, sz + s * 0.88f);
        handrail(a, b, true, C_IRON, 1.0f);
        SM.limb(b, V3(16.0f, top + 0.85f, sz + s * 0.88f), 0.05f, 0.05f, V3(0, 1, 0), C_IRON, MAT_METAL);
    }
    world.addGap("STOOP GAP", 200, 14.0f, sz, PI / 2, 0.9f, 1.5f, top + 0.05f);
    // low iron areaway fence along the sidewalk (a long flat rail)
    float fz0 = (i % 2) ? z0 + 0.3f : sz + 1.2f, fz1 = (i % 2) ? sz - 1.2f : z1 - 0.3f;
    if (fz1 - fz0 > 1.0f) handrail(V3(12.6f, SH + 0.8f, fz0), V3(12.6f, SH + 0.8f, fz1), true, C_IRON, 0.9f);
}

static void buildSE() {
    for (int i = 0; i < 7; i++) brownstone(30.f + i * 6.f, i);
    building(16, 72, 30, 200, 20, 0, hexc(0x6e4a3a), 70);
    facadeRow(V3(16, 0, 12), V3(-1, 0, 0), 18, 14, 71, true, 12, 20, 0);
    facadeRow(V3(30, 0, 12), V3(0, 0, -1), 14, 18, 72, true, 12, 20, 0);
    building(30, 45, 200, 200, 32, 1, hexc(0xb8ae9c), 73);
    building(69.5f, 12, 200, 45, 26, 0, hexc(0x7a4b3c), 74);
    // construction site behind a plywood fence, sidewalk shed over the sidewalk
    Col ply = hexc(0x2d5a8a);
    auto fenceSeg = [&](float xa, float xb) {
        float cx = (xa + xb) * 0.5f, hx = (xb - xa) * 0.5f;
        solidBox(cx, 12.35f, 0, hx, 0.08f, SH, SH + 2.45f, ply, MAT_PAINTED, SURF_WOOD);
    };
    fenceSeg(30, 44); fenceSeg(50, 69.5f);
    SM.text3D("POST NO BILLS", V3(43.0f, SH + 1.3f, 12.225f), V3(-1, 0, 0), V3(0, 1, 0), 0.07f, C_WHITE, MAT_PLAIN);
    SM.text3D("POST NO BILLS", V3(68.0f, SH + 1.3f, 12.225f), V3(-1, 0, 0), V3(0, 1, 0), 0.07f, C_WHITE, MAT_PLAIN);
    {   // posters + tags
        Rng r(77);
        static const uint32_t pc[] = {0xe23b3b, 0xf5d142, 0x3b7de2, 0xf08a24, 0xe8e4d8, 0x8a3be2};
        for (int i = 0; i < 9; i++) {
            float x = (i < 4 ? 36.0f : 54.0f) + (i % 4) * 1.6f + r.range(-0.2f, 0.2f);
            float y = SH + 0.4f + r.range(0, 0.5f);
            SM.boxAA(V3(x, y, 12.24f), V3(x + 1.1f, y + 1.5f, 12.26f), hexc(pc[r.irange(0, 5)]), MAT_PAINTED, 32);
        }
        SM.text3D("ZOOM", V3(61.5f, SH + 1.6f, 12.215f), V3(-1, 0, 0), V3(0, 1, 0), 0.12f, hexc(0xff4fa0), MAT_PLAIN);
        SM.text3D("NYC", V3(36.0f, SH + 2.0f, 12.215f), V3(-1, 0, 0), V3(0, 1, 0), 0.09f, hexc(0x40e0ff), MAT_PLAIN);
    }
    for (float x = 31.5f; x < 69; x += 3.0f) {
        for (int s = 0; s < 2; s++) {
            float z = s ? 12.05f : 7.75f;
            SM.limb(V3(x, SH, z), V3(x, SH + 3.3f, z), 0.1f, 0.1f, V3(1, 0, 0), hexc(0x3b5f3a), MAT_PAINTED);
            world.addBox(x, z, 0, 0.07f, 0.07f, SH, SH + 3.3f, SURF_METAL);
        }
    }
    SM.boxAA(V3(30.5f, SH + 3.3f, 7.5f), V3(69.5f, SH + 3.75f, 12.3f), hexc(0x3b5f3a), MAT_PAINTED);
    overlay(30, 12.45f, 69.5f, 45, SH + 0.003f, hexc(0x8d8272), MAT_CONCRETE);
    {   // dumpster with a plywood kicker leaning on it
        Col dc = hexc(0x2f6b3f), dt = hexc(0x3d7a4d);
        solidBox(38, 25, 0, 1.0f, 2.0f, SH, SH + 1.3f, dc, MAT_PAINTED, SURF_METAL, false, &dt);
        edgeRails(38, 25, 0, 1.0f, 2.0f, SH + 1.3f, RK_METAL, false);
        kicker(36.1f, 25, PI / 2, 1.1f, 0.9f, 1.3f);
        world.addGap("DUMPSTER HOP", 300, 38, 25, 0, 1.0f, 2.0f, SH + 1.4f);
    }
    for (int i = 0; i < 3; i++) {   // jersey barriers
        float cx = 48 + i * 5.2f, cz = 30;
        Col c = hexc(0xc9c5bc), t = hexc(0xd6d2c8);
        solidBox(cx, cz, PI / 2, 0.22f, 1.95f, SH, SH + 0.82f, c, MAT_CONCRETE, SURF_CONCRETE, false, &t);
        SM.boxRot(cx, cz, PI / 2, 0.34f, 1.95f, SH, SH + 0.25f, c, MAT_CONCRETE);
        addRailW(V3(cx - 1.95f, SH + 0.82f, cz), V3(cx + 1.95f, SH + 0.82f, cz), RK_LEDGE);
    }
    {   // pipe rail on cinder blocks
        V3 a(41, SH + 0.45f, 38.5f), b(53, SH + 0.45f, 38.5f);
        SM.limb(a, b, 0.1f, 0.1f, V3(0, 1, 0), hexc(0x9a7040), MAT_METAL);
        for (float t = 0.05f; t <= 1.0f; t += 0.3f) {
            V3 p = lerp3(a, b, t);
            SM.boxAA(V3(p.x - 0.2f, SH, p.z - 0.2f), V3(p.x + 0.2f, SH + 0.4f, p.z + 0.2f), hexc(0x8a8680), MAT_CONCRETE);
        }
        addRailW(a, b, RK_METAL);
        int si = world.addBox(47, 38.5f, PI / 2, 0.06f, 6, SH, SH + 0.4f, SURF_METAL);
        world.solids[si].noGround = true;
    }
    ledge(63, 22, 0, 1.6f, 2.6f, SH, 0.35f, hexc(0xb89a70), MAT_WOOD);   // stack of plywood
    {   // porta-potty
        solidBox(66.5f, 41, 0, 0.6f, 0.6f, SH, SH + 2.3f, hexc(0x2a5fb0), MAT_PAINTED, SURF_METAL);
    }
    world.addGap("SITE GAP", 400, 47, 20, 0, 3.0f, 1.0f, SH + 0.5f);
    letterPos.push_back(V3(38, SH + 3.0f, 25));   // 'T' high above the dumpster
    for (float z = 22; z < 70; z += 24) streetLamp(9.7f, z, SH, -PI / 2);
    streetLamp(40, 7.6f, SH, PI);
    npcPaths.push_back({{V3(11.0f, SH, 12), V3(11.0f, SH, 68)}, false});
    npcPaths.push_back({{V3(12.5f, SH, 10.0f), V3(68, SH, 10.0f)}, false});
    pigeonSpots.push_back(V3(13, SH, 50));
}

static void parkedCars() {
    Col cab = hexc(0xf2c318);
    Col cols[] = {hexc(0x1b1b1d), hexc(0x7a1f22), hexc(0x9ca3a8), hexc(0x274a78), hexc(0x2e4a2e), hexc(0xd8d4c8)};
    struct P { float x, z, yaw; int type; int col; };
    P cars[] = {
        {-60, -5.7f, PI / 2, 1, 0}, {-47, -5.7f, PI / 2, 0, 0}, {-33, -5.7f, PI / 2, 2, 0}, {-22, -5.7f, PI / 2, 1, 3},
        {22, 5.7f, PI / 2, 0, 0}, {36, 5.7f, PI / 2, 1, 1}, {50, 5.7f, PI / 2, 3, 5}, {62, 5.7f, PI / 2, 0, 0},
        {-7.7f, -48, 0, 1, 2}, {-7.7f, -36, 0, 0, 0}, {7.7f, 30, 0, 2, 0}, {7.7f, 46, 0, 1, 4}, {-7.7f, 40, 0, 0, 0},
        {-7.7f, 54, 0, 3, 5}, {7.7f, -20, 0, 1, 1}, {-40, -63.8f, PI / 2, 0, 0}, {-24, -63.8f, PI / 2, 1, 3}, {26, -63.8f, PI / 2, 2, 0}, {46, -63.8f, PI / 2, 0, 0},
    };
    for (const P& c : cars) parkedCar(c.x, c.z, c.yaw, c.type, c.type == 0 ? cab : cols[c.col]);
    letterPos.push_back(V3(22, 2.9f, 5.7f));   // 'K' over a parked cab
}

static void buildPromenade() {
    float top = SH;
    overlay(-600, -86.6f, 600, -76.4f, top + 0.003f, hexc(0x8b6d4e), MAT_WOOD);
    world.addBox(0, -81.5f, 0, 600, 5.1f, -0.3f, top, SURF_WOOD);
    // seawall + granite coping + railing
    SM.quadOut(V3(-600, top, RIVER_EDGE_Z), V3(600, top, RIVER_EDGE_Z), V3(600, WATER_LEVEL - 3, RIVER_EDGE_Z), V3(-600, WATER_LEVEL - 3, RIVER_EDGE_Z),
               V3(0, 0, -1), hexc(0x6f6a62), MAT_CONCRETE);
    SM.boxAA(V3(-600, top, RIVER_EDGE_Z), V3(600, top + 0.12f, RIVER_EDGE_Z + 0.5f), hexc(0x9a958c), MAT_GRANITE);
    handrail(V3(-200, top + 1.05f, RIVER_EDGE_Z + 0.25f), V3(200, top + 1.05f, RIVER_EDGE_Z + 0.25f), true, hexc(0x2b3a33), 2.0f);
    SM.limb(V3(-200, top + 0.55f, RIVER_EDGE_Z + 0.25f), V3(200, top + 0.55f, RIVER_EDGE_Z + 0.25f), 0.035f, 0.035f, V3(0, 1, 0), hexc(0x2b3a33), MAT_METAL);
    for (int i = -3; i <= 3; i++) {    // benches facing the river
        if (i == 0) continue;
        float x = i * 18.f, z = -84.3f;
        Col seat = hexc(0x8a6038), frame_ = hexc(0x22302a);
        solidBox(x, z, 0, 1.1f, 0.28f, top, top + 0.46f, frame_, MAT_WOOD, SURF_WOOD, false, &seat);
        SM.boxAA(V3(x - 1.1f, top + 0.5f, z + 0.24f), V3(x + 1.1f, top + 0.95f, z + 0.3f), seat, MAT_WOOD);
        int si = world.addBox(x, z + 0.27f, 0, 1.1f, 0.04f, top + 0.46f, top + 0.95f, SURF_WOOD);
        world.solids[si].noGround = true;
        addRailW(V3(x - 1.1f, top + 0.46f, z - 0.28f), V3(x + 1.1f, top + 0.46f, z - 0.28f), RK_WOOD);
    }
    for (float x = -64; x <= 64; x += 16) streetLamp(x, -75.2f, top, PI);
    for (float x = -56; x <= 56; x += 16) if (std::fabs(x) > 10) tree(x, -75.6f, top, 0.85f);
    pigeonSpots.push_back(V3(-20, top, -81));
    pigeonSpots.push_back(V3(30, top, -82));
    letterPos.push_back(V3(40, top + 1.05f + 0.85f, RIVER_EDGE_Z + 0.25f));   // 'E' above the river railing
    npcPaths.push_back({{V3(-68, top, -79.5f), V3(68, top, -79.5f)}, false});
    npcPaths.push_back({{V3(68, top, -82.6f), V3(-68, top, -82.6f)}, false});
    // river
    const int NX = 48, NZ = 24;
    float rx0 = -700, rx1 = 700, rz0 = -900, rz1 = RIVER_EDGE_Z;
    uint32_t base = (uint32_t)WM.v.size();
    for (int j = 0; j <= NZ; j++)
        for (int i = 0; i <= NX; i++) {
            float t = (float)j / NZ;
            float z = rz1 + (rz0 - rz1) * (t * t);    // denser near the shore
            WM.vert(V3(rx0 + (rx1 - rx0) * i / NX, WATER_LEVEL, z), V3(0, 1, 0), Col(0, 0, 0), 0);
        }
    for (int j = 0; j < NZ; j++)
        for (int i = 0; i < NX; i++) {
            uint32_t a = base + j * (NX + 1) + i, b = a + 1, c = a + NX + 1, d = c + 1;
            WM.idx.insert(WM.idx.end(), {a, c, b, b, c, d});
        }
}

static void buildSkyline() {
    Rng r(4242);
    static const uint32_t cols[] = {0x8e4a36, 0xb5ad9f, 0x7a3b2c, 0xc8bfae, 0x9c7a5a, 0x8899aa, 0x6e6a66};
    // Brooklyn across the river
    for (int i = 0; i < 70; i++) {
        float x = r.range(-650, 650), z = r.range(-620, -430), w = r.range(12, 40), d = r.range(12, 40), h = r.range(10, 45);
        if (r.chance(0.08f)) h = r.range(60, 110);
        building(x, z, x + w, z + d, h, r.chance(0.3f) ? 1 : 0, hexc(cols[r.irange(0, 6)]), r.next(), false);
    }
    SM.quadN(V3(-700, 0.5f, -420), V3(700, 0.5f, -420), V3(700, 0.5f, -900), V3(-700, 0.5f, -900), V3(0, 1, 0), hexc(0x5a574f), MAT_CONCRETE);
    SM.quadOut(V3(-700, 0.5f, -420), V3(700, 0.5f, -420), V3(700, WATER_LEVEL - 2, -420), V3(-700, WATER_LEVEL - 2, -420), V3(0, 0, 1), hexc(0x5a574f), MAT_CONCRETE);
    // Manhattan: a ring of towers behind the block
    for (int i = 0; i < 150; i++) {
        float a = r.range(-PI * 0.62f, PI * 0.62f);
        float rad = r.range(230, 520);
        float x = std::sin(a) * rad * 1.3f, z = std::cos(a) * rad;
        if (z < 60 && std::fabs(x) < 220) continue;
        float w = r.range(18, 40), d = r.range(18, 40), h = r.range(35, 150);
        int style = r.irange(0, 2);
        building(x - w / 2, z - d / 2, x + w / 2, z + d / 2, h, style, hexc(cols[r.irange(0, 6)]), r.next(), false);
        if (style != 2 && r.chance(0.25f)) {   // setback crown
            building(x - w / 4, z - d / 4, x + w / 4, z + d / 4, h + r.range(10, 30), style, hexc(cols[r.irange(0, 6)]), r.next(), false);
        }
    }
    // an art-deco spire tower down the avenue
    {
        float x = -40, z = 380;
        building(x - 30, z - 30, x + 30, z + 30, 120, 1, hexc(0xc9c0ae), 9001, false);
        building(x - 20, z - 20, x + 20, z + 20, 170, 1, hexc(0xc9c0ae), 9002, false);
        building(x - 12, z - 12, x + 12, z + 12, 215, 1, hexc(0xc9c0ae), 9003, false);
        SM.cylinder(frame(x, 215, z, 0), 5, 30, 8, hexc(0xd8d0c0), MAT_METAL, true, 0.4f);
        SM.cylinder(frame(x, 245, z, 0), 0.6f, 35, 6, hexc(0xb0b0b0), MAT_METAL, true, 0.1f);
    }
    // suspension bridge over the river to the west
    {
        float bx = -190, deckY = 38;
        float tz[2] = {-170, -350};
        Col stone = hexc(0x9a8c78);
        for (float z : tz) {
            for (int s = -1; s <= 1; s += 2) SM.boxAA(V3(bx + s * 9 - 3, WATER_LEVEL, z - 4), V3(bx + s * 9 + 3, 88, z + 4), stone, MAT_GRANITE);
            SM.boxAA(V3(bx - 12, 70, z - 4), V3(bx + 12, 80, z + 4), stone, MAT_GRANITE);
            SM.boxAA(V3(bx - 12, deckY - 4, z - 4), V3(bx + 12, deckY, z + 4), stone, MAT_GRANITE);
        }
        SM.boxAA(V3(bx - 11, deckY - 1.5f, -600), V3(bx + 11, deckY + 0.5f, -60), hexc(0x4a4a4a), MAT_METAL);
        for (int s = -1; s <= 1; s += 2) {
            float cx = bx + s * 9;
            auto cableY = [&](float z) {
                if (z > tz[0]) return 88 - (88 - deckY - 2) * sat((z - tz[0]) / 110.f);
                if (z < tz[1]) return 88 - (88 - deckY - 2) * sat((tz[1] - z) / 110.f);
                float t = (z - tz[1]) / (tz[0] - tz[1]);
                return 88 - 4 * (88 - deckY - 6) * t * (1 - t);
            };
            float prev = -560;
            for (float z = -560 + 10; z <= -60; z += 10) {
                SM.limb(V3(cx, cableY(prev), prev), V3(cx, cableY(z), z), 0.9f, 0.9f, V3(1, 0, 0), hexc(0x55504a), MAT_METAL);
                if (z < -65 && z > -555) SM.limb(V3(cx, deckY, z), V3(cx, cableY(z), z), 0.15f, 0.15f, V3(1, 0, 0), hexc(0x55504a), MAT_METAL);
                prev = z;
            }
        }
    }
}

static V3 SPAWN_POS(3.0f, 0.0f, 22.0f);
static float SPAWN_YAW = PI;

static void buildLevel() {
    buildStreets();
    buildNW();
    buildPlaza();
    buildCourt();
    buildSW();
    buildSE();
    parkedCars();
    buildPromenade();
    buildSkyline();
    letterPos.insert(letterPos.begin(), V3(28, SH + 5.4f, -34.5f));   // 'S' high above the fountain spout
    // order the letters S K A T E
    std::vector<V3> lp = letterPos;   // [S, A, T, K, E]
    letterPos = {lp[0], lp[3], lp[1], lp[2], lp[4]};
    world.buildGrid();
}

// ----------------------------------------------------------------------------
// Services implemented further down (audio, particles, HUD messages)
// ----------------------------------------------------------------------------
enum SfxId {
    SFX_POP = 0, SFX_LAND, SFX_LAND_HARD, SFX_GRIND, SFX_SLIDE, SFX_BAIL, SFX_SPLASH, SFX_COMBO, SFX_BIGCOMBO,
    SFX_LETTER, SFX_CLACK, SFX_FLIP, SFX_HONK, SFX_PIGEONS, SFX_HEY, SFX_MENU, SFX_GAP, SFX_COUNT
};
static void sfx(int id, float vol = 1.f, float pitch = 1.f);
static void spawnDust(V3 p, int n);
static void spawnSparks(V3 p, V3 v, int n);
static void spawnSplash(V3 p, int n, float power = 1.f);
static void popup(const std::string& s, Col c, float scale = 1.f, float life = 1.6f);
struct AudioParams { float roll = 0, rollPitch = 1, rollSurf = 0, grind = 0, grindMetal = 1, water = 0, wind = 0; };
static AudioParams aud;

// ----------------------------------------------------------------------------
// Tricks
// ----------------------------------------------------------------------------
// direction index: 0 none, 1 left, 2 right, 3 up, 4 down, 5 up+left, 6 up+right, 7 down+left, 8 down+right
static int dirIndex(bool up, bool down, bool left, bool right) {
    int v = up && !down ? 1 : (down && !up ? 2 : 0);
    int h = left && !right ? 1 : (right && !left ? 2 : 0);
    static const int map[3][3] = {{0, 1, 2}, {3, 5, 6}, {4, 7, 8}};
    return map[v][h];
}
struct FlipDef { const char* name; int pts; float dur, flip, shove, imp; };
static const FlipDef FLIPS[9] = {
    {"Kickflip", 100, 0.40f, 1, 0, 0},       {"Heelflip", 100, 0.40f, -1, 0, 0},
    {"Pop Shove-It", 100, 0.36f, 0, -0.5f, 0}, {"Impossible", 300, 0.48f, 0, 0, 1},
    {"360 Flip", 500, 0.52f, 1, -1, 0},       {"Hardflip", 400, 0.48f, 1, 0.5f, 0},
    {"Varial Kickflip", 300, 0.46f, 1, -0.5f, 0}, {"Varial Heelflip", 300, 0.46f, -1, 0.5f, 0},
    {"360 Shove-It", 250, 0.46f, 0, -1, 0}};
struct GrabDef { const char* name; int pts; };
static const GrabDef GRABS[9] = {
    {"Indy", 200}, {"Melon", 200}, {"Method", 300}, {"Nosegrab", 250}, {"Tailgrab", 250},
    {"Madonna", 450}, {"Benihana", 400}, {"Stalefish", 350}, {"Crossbone", 350}};
struct GrindDef { const char* name; int pts; bool slide; float yawOff, pitch, roll; };
static const GrindDef GRINDS[9] = {
    {"50-50 Grind", 100, false, 0, 0, 0},          {"Boardslide", 150, true, PI / 2, 0, 0},
    {"Feeble Grind", 250, false, 0.35f, 0.14f, 0.2f}, {"Nosegrind", 200, false, 0, -0.26f, 0},
    {"5-0 Grind", 150, false, 0, 0.26f, 0},         {"Noseslide", 200, true, PI / 2, -0.16f, 0},
    {"Crooked Grind", 300, false, -0.42f, -0.24f, 0.18f}, {"Tailslide", 250, true, PI / 2, 0.16f, 0},
    {"Smith Grind", 300, false, 0.42f, 0.24f, -0.2f}};
static const int SPIN_PTS[8] = {0, 150, 400, 750, 1200, 2000, 3000, 4500};

struct Combo {
    std::vector<std::string> names;
    std::vector<std::pair<std::string, int>> uses;
    float base = 0;
    int mult = 0;
    bool active() const { return mult > 0; }
    void reset() { names.clear(); uses.clear(); base = 0; mult = 0; }
    void add(const std::string& n, float pts) {
        int u = 0;
        for (auto& p : uses) if (p.first == n) { u = ++p.second; break; }
        if (u == 0) uses.push_back({n, 0});
        float k = std::max(0.25f, std::pow(0.72f, (float)u));   // repeated tricks are worth less
        base += pts * k;
        mult++;
        if (!names.empty() && names.back() == n) names.back() += "+";
        names.push_back(n);
    }
    void addRunning(float pts) { base += pts; }
    long long value() const { return (long long)base * std::max(1, mult); }
    std::string text(size_t maxChars) const {
        std::string s;
        for (int i = (int)names.size() - 1; i >= 0; i--) {
            std::string part = names[i];
            if (part.back() == '+') continue;
            std::string ns = part + (s.empty() ? "" : " + ") + s;
            if (ns.size() > maxChars) { s = "... + " + s; break; }
            s = ns;
        }
        return s;
    }
};

// ----------------------------------------------------------------------------
// Input snapshot for one physics tick
// ----------------------------------------------------------------------------
struct Input {
    bool up = false, down = false, left = false, right = false;
    bool ollie = false, flip = false, grab = false, grind = false, manual = false;           // held
    bool olliePress = false, ollieRelease = false, flipPress = false, grabPress = false;     // edges
    bool grindPress = false, manualPress = false;
};

enum PState { ST_RIDE = 0, ST_AIR, ST_GRIND, ST_MANUAL, ST_BAIL };

static const float GRAV = 12.5f;
static const float OLLIE_MIN = 5.2f, OLLIE_MAX = 6.8f;
static const float PUSH_ACC = 5.5f, PUSH_MAX = 10.2f, MAX_SPEED = 19.f;
static const float SPIN_MAX = 9.2f, SPIN_ACC = 40.f;

struct Player {
    V3 pos, vel, prevPos;   // prevPos: position at the previous physics tick (render interpolation)
    float yaw = 0;
    int state = ST_RIDE;
    V3 n = V3(0, 1, 0), upVis = V3(0, 1, 0);
    int groundSolid = -1, surf = SURF_ASPHALT;
    float speed = 0;
    // ollie
    bool crouching = false;
    float crouchT = 0, coyote = 0;   // coyote: late-ollie window after rolling off an edge
    // air
    float airT = 0, spinRate = 0, spinAccum = 0;
    bool qpAir = false, fromManual = false, trickThisAir = false;
    int flipIdx = -1, flipExtra = 0;
    float flipT = 0, flipDur = 0;
    int grabIdx = -1;
    float grabT = 0, grabBlend = 0;
    bool grabHeld = false;
    std::vector<int> gapArmed;       // gaps flown over during this air
    std::vector<int> gapDone;        // gaps already awarded in this combo
    // grind
    int rail = -1, railDir = 1, grindIdx = 0, lastRail = -1;
    float railT = 0, railSpeed = 0, grindTime = 0, railCooldown = 0, grindBuffer = 0, grindYaw = 0;
    float bal = 0, balV = 0, balSeed = 0;
    bool grindFakie = false;
    // manual
    bool nose = false;
    float manualTime = 0, manualBuffer = 0;
    // bail
    float bailT = 0;
    std::string bailWhy;
    V3 bodyPos, bodyVel, boardPos, boardVel;
    float boardSpin = 0, boardAng = 0, bodyAng = 0;
    // animation helpers
    float pushPhase = 0, landSquash = 0, sparkAcc = 0;
    bool pushing = false, braking = false;
    // scoring
    Combo combo;
    float landGrace = 0;
    long long score = 0, best = 0;
    bool letters[5] = {false, false, false, false, false};
    int lettersGot = 0;
    float slabDist = 0;
    Rng rng = Rng(777);

    void reset(V3 p, float y) {
        Player fresh;
        fresh.score = score; fresh.best = best;
        for (int i = 0; i < 5; i++) fresh.letters[i] = letters[i];
        fresh.lettersGot = lettersGot;
        *this = fresh;
        pos = p; yaw = y;
        pos.y = world.ground(p.x, p.z, p.y + 1.0f).h;
    }
    bool grounded() const { return state == ST_RIDE || state == ST_MANUAL; }
    bool isFakie() const { return dot(vel, fwdYaw(yaw)) < -0.3f; }

    // ---------------------------------------------------------------- scoring
    void bankCombo() {
        if (!combo.active()) return;
        long long v = combo.value();
        score += v;
        if (score > best) best = score;
        const char* word = v >= 50000 ? "INSANE!!" : v >= 15000 ? "SICK COMBO!" : v >= 5000 ? "SWEET!" : v >= 1500 ? "NICE!" : "LANDED";
        char buf[64];
        snprintf(buf, sizeof buf, "%s  +%lld", word, v);
        popup(buf, v >= 5000 ? hexc(0xffd23a) : hexc(0x7dff8a), v >= 5000 ? 1.35f : 1.1f, 2.0f);
        sfx(v >= 5000 ? SFX_BIGCOMBO : SFX_COMBO, 0.8f);
        combo.reset();
        gapDone.clear();
    }
    void addTrick(const std::string& name, float pts) {
        combo.add(name, pts);
        landGrace = 0;
    }
    void finishSpin() {
        int n180 = (int)std::floor(std::fabs(spinAccum) / PI + 0.5f);
        if (n180 >= 1) {
            n180 = std::min(n180, 7);
            char buf[32];
            bool bs = (spinAccum > 0) != isFakie();
            snprintf(buf, sizeof buf, "%s %d", bs ? "BS" : "FS", n180 * 180);
            addTrick(buf, (float)SPIN_PTS[n180]);
        }
        spinAccum = 0;
        spinRate = 0;
    }

    // ---------------------------------------------------------------- bail
    void bail(const std::string& why, bool splash = false) {
        if (state == ST_BAIL) return;
        state = ST_BAIL;
        bailT = 0;
        bailWhy = why;
        if (combo.active()) {
            char buf[80];
            snprintf(buf, sizeof buf, "BAILED!  -%lld", combo.value());
            popup(buf, hexc(0xff4a3a), 1.3f, 2.0f);
        } else popup("BAILED!", hexc(0xff4a3a), 1.3f, 1.6f);
        if (!why.empty()) popup(why, hexc(0xffb0a0), 0.8f, 1.8f);
        combo.reset();
        gapDone.clear();
        landGrace = 0;
        V3 side = cross(V3(0, 1, 0), fwdYaw(yaw));
        bodyPos = pos;
        bodyVel = vel * 0.55f + V3(0, 2.2f, 0);
        boardPos = pos + V3(0, 0.1f, 0);
        boardVel = vel * 1.05f + V3(0, 2.6f, 0) + side * rng.range(-2.f, 2.f);
        boardSpin = rng.range(-12.f, 12.f);
        boardAng = 0;
        bodyAng = 0;
        flipIdx = grabIdx = -1;
        rail = -1;
        crouching = false;
        sfx(splash ? SFX_SPLASH : SFX_BAIL);
        if (splash) spawnSplash(pos, 80, 1.5f); else spawnDust(pos, 14);
    }
    void updateBail(float dt) {
        bailT += dt;
        auto phys = [&](V3& p, V3& v, float bounce) {
            v.y -= GRAV * dt;
            p += v * dt;
            float gy = world.ground(p.x, p.z, p.y + 0.4f).h;
            if (p.z < RIVER_EDGE_Z && p.y < WATER_LEVEL) {   // sinks slowly in the river
                p.y = WATER_LEVEL - 0.2f; v = v * 0.9f; v.y = 0;
            } else if (p.y < gy) {
                p.y = gy;
                if (v.y < -2.f) v.y = -v.y * bounce; else v.y = 0;
                v.x *= 0.88f; v.z *= 0.88f;
            }
            V3 dummy = v;
            world.collideWalls(p, dummy, 0.25f, p.y + 0.3f, 0.8f);
            v.x = dummy.x; v.z = dummy.z;
        };
        phys(bodyPos, bodyVel, 0.25f);
        phys(boardPos, boardVel, 0.45f);
        boardAng += boardSpin * dt;
        boardSpin *= std::exp(-1.5f * dt);
        bodyAng = std::min(1.f, bailT * 3.0f);
        pos = bodyPos;
        vel = bodyVel;
        if (bailT > 1.8f) respawnAfterBail();
    }
    void respawnAfterBail() {
        V3 p = bodyPos;
        if (p.z < RIVER_EDGE_Z + 0.8f) { p.z = RIVER_EDGE_Z + 3.0f; p.y = SH + 1.0f; }
        p.y = world.ground(p.x, p.z, std::max(p.y, 0.f) + 0.5f).h;
        // never respawn perched on a thin thing: drop to the lowest nearby surface if on a rail blocker
        V3 v(0, 0, 0);
        world.collideWalls(p, v, 0.35f, p.y + STEP_UP, 1.6f);
        p.y = world.ground(p.x, p.z, p.y + 0.3f).h;
        float y = yaw;
        state = ST_RIDE;
        pos = p;
        vel = V3(0, 0, 0);
        yaw = y;
        spinAccum = spinRate = 0;
        flipIdx = grabIdx = -1;
        rail = -1;
        crouching = false;
        landSquash = 0;
    }

    // ---------------------------------------------------------------- ollie
    void ollie(float strength, bool fromRail) {
        float v = lerpf(OLLIE_MIN, OLLIE_MAX, strength);
        if (fromRail) v *= 0.92f;
        // pop straight up (popping along a ramp normal would bleed forward speed on kickers)
        if (vel.y < 0 && !fromRail) vel.y *= 0.5f;
        vel += V3(0, 1, 0) * v;
        if (!fromRail && n.y < 0.6f) qpAir = true;
        fromManual = false;
        state = ST_AIR;
        airT = 0;
        qpAir = false;
        trickThisAir = false;
        crouching = false;
        crouchT = 0;
        coyote = 0;
        gapArmed.clear();
        sfx(SFX_POP, 0.9f, 0.95f + strength * 0.15f);
    }
    void startFlip(int idx) {
        if (flipIdx >= 0) {
            // same flip pressed again mid-trick => double / triple
            if (flipIdx == idx && flipExtra < 2 && flipT > flipDur * 0.35f) {
                flipExtra++;
                flipDur += FLIPS[idx].dur * 0.75f;
                const char* pre = flipExtra == 1 ? "Double " : "Triple ";
                addTrick(std::string(pre) + FLIPS[idx].name, FLIPS[idx].pts * (flipExtra == 1 ? 2.2f : 3.6f));
                sfx(SFX_FLIP, 0.5f, 1.15f);
            }
            return;
        }
        if (grabIdx >= 0) return;
        flipIdx = idx;
        flipT = 0;
        flipExtra = 0;
        flipDur = FLIPS[idx].dur;
        addTrick(FLIPS[idx].name, (float)FLIPS[idx].pts);
        trickThisAir = true;
        sfx(SFX_FLIP, 0.5f);
    }
    void startGrab(int idx) {
        if (flipIdx >= 0 || grabIdx >= 0) return;
        grabIdx = idx;
        grabT = 0;
        addTrick(GRABS[idx].name, (float)GRABS[idx].pts);
        trickThisAir = true;
    }

    // ---------------------------------------------------------------- ground
    void updateGround(const Input& in, float dt) {
        bool manual = state == ST_MANUAL;
        GroundHit g0 = world.ground(pos.x, pos.z, pos.y + 0.05f);
        n = g0.n;
        speed = len(vel);
        // steering
        float steer = (in.left ? 1.f : 0.f) - (in.right ? 1.f : 0.f);
        float rate = speed < 0.7f ? 2.6f : lerpf(3.2f, 2.2f, sat((speed - 1.f) / 11.f));
        if (manual) rate *= 0.7f;
        if (crouching) rate *= 1.15f;
        yaw += steer * rate * dt;
        V3 f = fwdYaw(yaw);
        V3 ft = norm(f - n * dot(f, n));
        V3 g(0, -GRAV, 0);
        vel += (g - n * dot(g, n)) * dt;
        float s = dot(vel, ft);
        V3 lat = vel - ft * s - n * dot(vel, n);
        pushing = false;
        braking = false;
        float travel = s < -0.15f ? -1.f : 1.f;
        if (in.up && !manual && !crouching && std::fabs(s) < PUSH_MAX) {
            s += travel * PUSH_ACC * dt;
            pushing = true;
        }
        if (in.down && !manual) {
            s = approach(s, 0, 6.5f * dt);
            braking = std::fabs(s) > 0.5f;
        }
        s = approach(s, 0, (0.22f + 0.0035f * s * s) * dt);
        lat = lat * std::exp(-11.f * dt);
        vel = ft * s + lat;
        if (len(vel) > MAX_SPEED) vel = norm(vel) * MAX_SPEED;
        pushPhase = pushing ? pushPhase + dt * 1.8f : std::max(0.f, pushPhase - dt * 2.f);
        if (!pushing && pushPhase > 1) pushPhase = 0;

        // move + collide
        V3 np = pos + vel * dt;
        V3 wallN;
        bool onQP = groundSolid >= 0 && world.solids[groundSolid].type == S_QP;
        // on a slope, the ground ahead is higher than under our feet: allow for that when testing walls
        float slopeRise = n.y < 0.999f ? 0.3f * std::sqrt(std::max(0.f, 1 - n.y * n.y)) / std::max(n.y, 0.3f) : 0.f;
        float impact = world.collideWalls(np, vel, onQP ? 0.05f : 0.3f, pos.y + STEP_UP + slopeRise, 1.6f, &wallN);
        if (impact > 9.5f) { pos = np; bail("SLAMMED INTO A WALL"); return; }
        if (impact > 2.5f) { sfx(SFX_LAND, 0.4f, 0.7f); landSquash = 0.3f; }
        GroundHit g2 = world.ground(np.x, np.z, pos.y + STEP_UP);
        float dy = np.y - g2.h;
        float snap = 0.035f + len(vel) * dt * 0.55f;
        bool leaving = dy > 0.002f && dot(vel, g2.n) > 0.8f;   // moving away from the next surface: launch
        if (dy <= snap && !leaving) {
            if (g2.surf == SURF_WATER) { pos = np; bail("SPLASHDOWN!", true); return; }
            float rise = g2.h - pos.y;
            if (rise > 0.06f && g2.n.y > 0.97f) {                // rolled up a curb
                vel = vel * 0.86f;
                landSquash = 0.5f;
                sfx(SFX_CLACK, 0.7f);
            }
            np.y = g2.h;
            float sp = len(vel);
            V3 nv = vel - g2.n * dot(vel, g2.n);
            float kink = dot(g2.n, n);
            if (len(nv) > 1e-4f) vel = norm(nv) * (sp * (kink < 0.93f ? 0.96f : 1.f));
            n = g2.n;
            groundSolid = g2.solid;
            surf = g2.surf;
            pos = np;
        } else {
            // left the ground: off a ledge, over a lip or out of a quarter pipe
            const Solid* last = groundSolid >= 0 ? &world.solids[groundSolid] : nullptr;
            pos = np;
            fromManual = manual;
            state = ST_AIR;
            airT = 0;
            trickThisAir = false;
            gapArmed.clear();
            qpAir = false;
            if (last && last->type == S_QP && n.y < 0.6f) {
                // vert assist: come straight back down the transition
                V3 fz = last->localDirZ();
                V3 vh(vel.x, 0, vel.z);
                float along = dot(vh, fz);
                V3 latv = vh - fz * along;
                vel = latv + fz * -0.35f + V3(0, vel.y, 0);
                qpAir = true;
            }
            coyote = qpAir ? 0.f : 0.14f;
            if (qpAir) crouching = false;
            return;
        }
        // ollie charge / release
        if (in.olliePress && !crouching) { crouching = true; crouchT = 0; }
        if (crouching) {
            crouchT += dt;
            if (!in.ollie) { ollie(sat(crouchT / 0.32f), false); return; }
        }
        if (in.flipPress) {   // pop straight into a flip
            ollie(0.45f, false);
            startFlip(dirIndex(in.up, in.down, in.left, in.right));
            return;
        }
        if (in.grindPress) { grindBuffer = 0.45f; ollie(0.25f, false); return; }
        // manuals
        if (state == ST_RIDE && in.manualPress && len(vel) > 1.0f) startManual(in);
        else if (state == ST_MANUAL && in.manualPress && manualTime > 0.15f) { state = ST_RIDE; bankCombo(); }
        if (state == ST_MANUAL) {
            manualTime += dt;
            float inst = 1.8f + manualTime * 0.35f;
            balV += (bal * inst * 2.4f + std::sin(manualTime * 2.3f + balSeed) * (0.5f + 0.15f * manualTime)) * dt;
            if (in.up) balV -= 6.f * dt;
            if (in.down) balV += 6.f * dt;
            balV *= std::exp(-1.2f * dt);
            bal += balV * dt;
            combo.addRunning(140.f * dt);
            if (std::fabs(bal) >= 1.f) { bail(bal > 0 ? "SCRAPED THE TAIL" : "NOSE DIVE"); return; }
            if (len(vel) < 0.6f) { state = ST_RIDE; bankCombo(); }
        }
        speed = len(vel);
        // rolling sound + sidewalk joint clacks
        if (state != ST_AIR) {
            slabDist += speed * dt;
            if ((surf == SURF_SIDEWALK) && slabDist > 1.52f) { slabDist = 0; sfx(SFX_CLACK, 0.18f + speed * 0.02f, 1.3f); }
        }
    }
    void startManual(const Input& in) {
        state = ST_MANUAL;
        nose = in.up;
        manualTime = 0;
        bal = rng.range(-0.1f, 0.1f);
        balV = 0;
        balSeed = rng.range(0, 10);
        addTrick(nose ? "Nose Manual" : "Manual", nose ? 150.f : 100.f);
    }

    // ---------------------------------------------------------------- air
    void land(const GroundHit& g, const Input& in) {
        V3 nn = g.n;
        float impact = -dot(vel, nn);
        V3 vt = vel - nn * dot(vel, nn);
        V3 f = fwdYaw(yaw);
        V3 ft = norm(f - nn * dot(f, nn));
        std::string fail;
        bool splash = false;
        if (g.surf == SURF_WATER) { fail = "SPLASHDOWN!"; splash = true; }
        else if (flipIdx >= 0 && flipT < flipDur * 0.8f) fail = "DIDN'T FINISH THE FLIP";
        else if (grabIdx >= 0 && grabHeld && grabT > 0.08f) fail = "STILL GRABBING";
        else {
            float vl = len(vt);
            if (vl > 1.8f) {
                float c = dot(ft, vt / vl);
                if (std::fabs(c) < 0.74f) fail = "LANDED SIDEWAYS";
            }
            if (upVis.y < 0.2f && nn.y > 0.8f) fail = "LANDED ON YOUR HEAD";
        }
        pos.y = g.h;
        if (!fail.empty()) { bail(fail, splash); return; }
        float vl = len(vt);
        if (vl > 1.0f) {   // straighten out to the travel direction (forward or fakie)
            float d = wrapPi(yawOf(vt) - yaw);
            if (std::fabs(d) > PI / 2) d = wrapPi(d + PI);
            yaw += d;
        }
        vel = vt * (impact > 11.f ? 0.82f : 0.97f);
        n = nn;
        groundSolid = g.solid;
        surf = g.surf;
        if (trickThisAir || spinAccum != 0) finishSpin();
        flipIdx = -1;
        grabIdx = -1;
        grabBlend = 0;
        spinRate = 0;
        qpAir = false;
        landSquash = std::min(1.f, 0.35f + impact * 0.06f);
        sfx(impact > 9 ? SFX_LAND_HARD : SFX_LAND, std::min(1.f, 0.4f + impact * 0.07f));
        spawnDust(pos, impact > 6 ? 10 : 4);
        state = ST_RIDE;
        bool wantManual = in.manual || manualBuffer > 0 || (fromManual && airT < 0.45f);
        if (wantManual && len(vel) > 1.0f) {
            if (fromManual && airT < 0.45f && !trickThisAir && combo.active()) { state = ST_MANUAL; }   // kept manualing off a curb
            else startManual(in);
            manualBuffer = 0;
        } else if (combo.active()) {
            landGrace = 0.3f;
        }
        fromManual = false;
    }
    void updateAir(const Input& in, float dt) {
        airT += dt;
        vel.y -= GRAV * dt;
        // late ollie: releasing (or tapping) space just after rolling off an edge still pops
        coyote -= dt;
        if (coyote > 0 && in.olliePress && !crouching) { crouching = true; crouchT = 0.1f; }
        if (crouching) {
            crouchT += dt;
            if (!in.ollie) {
                bool late = coyote > 0;
                crouching = false;
                if (late) { ollie(sat(crouchT / 0.32f), false); return; }
            }
        }
        // spins
        float spinIn = (in.left ? 1.f : 0.f) - (in.right ? 1.f : 0.f);
        if (spinIn != 0) spinRate = approach(spinRate, spinIn * SPIN_MAX, SPIN_ACC * dt);
        else spinRate = approach(spinRate, 0, SPIN_ACC * 1.5f * dt);
        yaw += spinRate * dt;
        spinAccum += spinRate * dt;
        // a little air control
        V3 fwd = norm(V3(vel.x, 0, vel.z));
        if (in.up) vel += fwd * (0.6f * dt);
        if (in.down) vel -= fwd * (0.6f * dt);
        // tricks
        if (in.flipPress) startFlip(dirIndex(in.up, in.down, in.left, in.right));
        if (in.grabPress) startGrab(dirIndex(in.up, in.down, in.left, in.right));
        grabHeld = in.grab;
        if (flipIdx >= 0) {
            flipT += dt;
            if (flipT >= flipDur) { flipIdx = -1; flipT = 0; }
        }
        if (grabIdx >= 0) {
            grabT += dt;
            if (in.grab) {
                grabBlend = std::min(1.f, grabBlend + dt * 9.f);
                if (grabT > 0.2f) combo.addRunning(320.f * dt);
            } else {
                grabBlend = std::max(0.f, grabBlend - dt * 10.f);
                if (grabBlend <= 0) grabIdx = -1;
            }
        }
        if (in.manualPress) manualBuffer = 0.35f;
        if (in.grindPress) grindBuffer = 0.3f;
        if ((in.grind || grindBuffer > 0) && tryGrind(in)) return;
        // gaps: arm while above a gap zone, award when we have cleared it
        for (int i = 0; i < (int)world.gaps.size(); i++) {
            const Gap& gp = world.gaps[i];
            bool over = gp.inside(pos.x, pos.z) && pos.y > gp.topY;
            bool armed = std::find(gapArmed.begin(), gapArmed.end(), i) != gapArmed.end();
            if (over && !armed) gapArmed.push_back(i);
        }
        // integrate
        V3 np = pos + vel * dt;
        world.collideWalls(np, vel, qpAir ? 0.05f : 0.3f, pos.y + 0.12f, 1.6f);
        GroundHit g = world.ground(np.x, np.z, pos.y + 0.05f);
        // visual up vector leans towards the surface below (keeps vert airs looking right)
        V3 targetUp = qpAir ? norm(lerp3(V3(0, 1, 0), g.n, 0.75f)) : V3(0, 1, 0);
        upVis = norm(damp3(upVis, targetUp, 5.f, dt));
        if (np.y <= g.h && vel.y <= 0.5f) {
            pos = np;
            awardGaps();
            if (state != ST_AIR) return;
            land(g, in);
            return;
        }
        pos = np;
        if (pos.z < RIVER_EDGE_Z && pos.y < WATER_LEVEL + 0.2f) { bail("SWIM TIME!", true); return; }
    }
    void awardGaps() {
        for (int i : gapArmed) {
            const Gap& gp = world.gaps[i];
            if (gp.inside(pos.x, pos.z)) continue;   // landed on it, not over it
            if (std::find(gapDone.begin(), gapDone.end(), i) != gapDone.end()) continue;
            gapDone.push_back(i);
            addTrick(gp.name, (float)gp.points);
            popup(gp.name, hexc(0x9fe8ff), 0.9f, 1.3f);
            sfx(SFX_GAP, 0.6f);
            trickThisAir = true;
        }
        gapArmed.clear();
    }

    bool tryGrind(const Input& in);
    void startGrind(int r, float t, int dir, float spd, const Input& in);
    void updateGrind(const Input& in, float dt);
    void exitGrind(bool popUp);
    void update(const Input& in, float dt);
};
static Player P;

// ----------------------------------------------------------------------------
// Grinding, the per-tick player update, S-K-A-T-E letters
// ----------------------------------------------------------------------------
bool Player::tryGrind(const Input& in) {
    int best = -1;
    float bestScore = 1e9f, bestT = 0;
    for (int i = 0; i < (int)world.rails.size(); i++) {
        if (i == lastRail && railCooldown > 0) continue;
        const Rail& r = world.rails[i];
        float t = clampf(dot(pos - r.a, r.dir), 0.f, r.L);
        V3 d = pos - (r.a + r.dir * t);
        float dh = std::sqrt(d.x * d.x + d.z * d.z);
        if (dh > 1.1f || d.y < -0.55f || d.y > 0.85f) continue;
        if (vel.y > 3.0f && d.y < 0) continue;      // still rising up past it
        float sc = dh + std::fabs(d.y) * 0.6f;
        if (sc < bestScore) { bestScore = sc; best = i; bestT = t; }
    }
    if (best < 0) return false;
    const Rail& r = world.rails[best];
    V3 rh(r.dir.x, 0, r.dir.z);
    float rl = len(rh);
    if (rl < 0.2f) return false;
    rh = rh / rl;
    V3 vh(vel.x, 0, vel.z);
    float spd = len(vh), along = dot(vh, rh);
    if (spd > 1.0f && std::fabs(along) < 0.25f * spd) return false;   // coming in dead perpendicular
    int dir = spd > 1.0f ? (along >= 0 ? 1 : -1) : (dot(fwdYaw(yaw), rh) >= 0 ? 1 : -1);
    if ((dir > 0 && r.L - bestT < 0.25f) || (dir < 0 && bestT < 0.25f)) return false;
    float s = std::max(std::fabs(dot(vel, r.dir)), std::max(0.8f * spd, 3.0f));
    startGrind(best, bestT, dir, s, in);
    return state == ST_GRIND;
}

void Player::startGrind(int ri, float t, int dir, float spd, const Input& in) {
    if (state == ST_AIR) {
        if (flipIdx >= 0 && flipT < flipDur * 0.7f) { bail("DIDN'T FINISH THE FLIP"); return; }
        awardGaps();
        finishSpin();
    }
    const Rail& r = world.rails[ri];
    flipIdx = -1;
    grabIdx = -1;
    grabBlend = 0;
    state = ST_GRIND;
    rail = ri;
    railT = t;
    railDir = dir;
    railSpeed = std::min(spd, 14.f);
    grindIdx = dirIndex(in.up, in.down, in.left, in.right);
    grindTime = 0;
    bal = rng.range(-0.15f, 0.15f);
    balV = 0;
    balSeed = rng.range(0, 10);
    V3 md = r.dir * (float)dir;
    float travelYaw = yawOf(V3(md.x, 0, md.z));
    grindFakie = std::cos(wrapPi(yaw - travelYaw)) < 0;
    const GrindDef& gd = GRINDS[grindIdx];
    float base = travelYaw + (grindFakie ? PI : 0);
    if (gd.slide) {
        float a1 = base + PI / 2, a2 = base - PI / 2;
        grindYaw = std::fabs(wrapPi(a1 - yaw)) < std::fabs(wrapPi(a2 - yaw)) ? a1 : a2;
    } else grindYaw = base + gd.yawOff;
    yaw = grindYaw;
    pos = r.a + r.dir * railT;
    vel = md * railSpeed;
    addTrick(gd.name, (float)gd.pts);
    bool slideSound = gd.slide || r.kind != RK_METAL;
    sfx(slideSound ? SFX_SLIDE : SFX_GRIND, 0.85f);
    landSquash = 0.45f;
    grindBuffer = 0;
    spinRate = 0;
    spinAccum = 0;
    upVis = V3(0, 1, 0);
    crouching = false;
}

void Player::exitGrind(bool popUp) {
    const Rail& r = world.rails[rail];
    lastRail = rail;
    railCooldown = 0.3f;
    V3 md = r.dir * (float)railDir;
    vel = md * railSpeed;
    if (!popUp) vel.y += 1.4f;
    yaw = yawOf(V3(md.x, 0, md.z)) + (grindFakie ? PI : 0);
    state = ST_AIR;
    airT = 0;
    trickThisAir = false;
    qpAir = false;
    gapArmed.clear();
    rail = -1;
    pos.y += 0.02f;
}

void Player::updateGrind(const Input& in, float dt) {
    const Rail& r = world.rails[rail];
    V3 md = r.dir * (float)railDir;
    grindTime += dt;
    bool metal = r.kind == RK_METAL;
    railSpeed += dot(V3(0, -GRAV, 0), md) * dt * 0.85f;
    railSpeed -= (metal ? 0.6f : 1.2f) * dt;
    // balance meter: unstable equilibrium you fight with left/right
    float inst = 2.0f + grindTime * 0.45f;
    balV += (bal * inst * 2.2f + std::sin(grindTime * 2.9f + balSeed) * (0.5f + grindTime * 0.18f)) * dt;
    if (in.left) balV -= 6.5f * dt;
    if (in.right) balV += 6.5f * dt;
    balV *= std::exp(-1.3f * dt);
    bal += balV * dt;
    combo.addRunning((metal ? 130.f : 110.f) * dt);
    if (std::fabs(bal) >= 1.f) { bail(bal > 0 ? "FELL OFF THE RAIL" : "LOST YOUR BALANCE"); return; }
    if (in.olliePress) { exitGrind(true); ollie(0.55f, true); return; }
    // sparks off metal
    if (metal) {
        sparkAcc += dt * (20.f + railSpeed * 6.f);
        while (sparkAcc >= 1.f) { sparkAcc -= 1.f; spawnSparks(pos, md * railSpeed, 1); }
    }
    railT += railSpeed * railDir * dt;
    if (railSpeed < 0.5f) { exitGrind(false); return; }
    if (railT < 0 || railT > r.L) {
        V3 endP = railT < 0 ? r.a : r.b;
        float over = railT < 0 ? -railT : railT - r.L;
        int next = -1, nextDir = 1;
        float nextT = 0;
        for (int i = 0; i < (int)world.rails.size(); i++) {
            if (i == rail) continue;
            const Rail& q = world.rails[i];
            if (len(q.a - endP) < 0.15f && dot(q.dir, md) > 0.6f) { next = i; nextDir = 1; nextT = over; break; }
            if (len(q.b - endP) < 0.15f && dot(q.dir * -1.f, md) > 0.6f) { next = i; nextDir = -1; nextT = q.L - over; break; }
        }
        if (next < 0) { exitGrind(false); return; }
        V3 nmd = world.rails[next].dir * (float)nextDir;
        yaw += wrapPi(yawOf(V3(nmd.x, 0, nmd.z)) - yawOf(V3(md.x, 0, md.z)));
        grindYaw = yaw;
        rail = next;
        railDir = nextDir;
        railT = nextT;
    }
    const Rail& rr = world.rails[rail];
    pos = rr.a + rr.dir * railT;
    vel = rr.dir * (float)railDir * railSpeed;
    speed = railSpeed;
}

void Player::update(const Input& in, float dt) {
    // defensive: never let a numerical blow-up leave the skater in limbo
    if (!(std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z) && std::isfinite(vel.x) && std::isfinite(vel.y) && std::isfinite(vel.z))) {
        reset(SPAWN_POS, SPAWN_YAW);
        return;
    }
    if (pos.y < -30.f) { respawnAfterBail(); return; }
    railCooldown = std::max(0.f, railCooldown - dt);
    grindBuffer = std::max(0.f, grindBuffer - dt);
    manualBuffer = std::max(0.f, manualBuffer - dt);
    landSquash = std::max(0.f, landSquash - dt * 2.5f);
    switch (state) {
        case ST_RIDE:
        case ST_MANUAL: updateGround(in, dt); break;
        case ST_AIR: updateAir(in, dt); break;
        case ST_GRIND: updateGrind(in, dt); break;
        case ST_BAIL: updateBail(dt); break;
    }
    if (state == ST_RIDE && landGrace > 0) {
        landGrace -= dt;
        if (landGrace <= 0) bankCombo();
    }
    if (state == ST_RIDE || state == ST_MANUAL) upVis = norm(damp3(upVis, n, 14.f, dt));
    else if (state == ST_GRIND) upVis = norm(damp3(upVis, V3(0, 1, 0), 14.f, dt));
    // S-K-A-T-E letters
    if (state != ST_BAIL) {
        static const char* L[5] = {"S", "K", "A", "T", "E"};
        for (int i = 0; i < 5 && i < (int)letterPos.size(); i++) {
            if (letters[i]) continue;
            if (len(pos + V3(0, 0.9f, 0) - letterPos[i]) < 1.4f) {
                letters[i] = true;
                lettersGot++;
                score += 250;
                if (score > best) best = score;
                sfx(SFX_LETTER);
                if (lettersGot == 5) {
                    score += 5000;
                    if (score > best) best = score;
                    popup("S-K-A-T-E COLLECTED!  +5000", hexc(0xffd23a), 1.4f, 3.0f);
                } else popup(std::string("LETTER ") + L[i] + "  +250", hexc(0xff9ad8), 1.1f, 1.6f);
            }
        }
    }
    // continuous audio parameters
    bool rolling = state == ST_RIDE || state == ST_MANUAL;
    aud.roll = rolling ? sat(speed / 9.f) * (surf == SURF_WOOD ? 0.8f : 0.65f) : 0.f;
    aud.rollPitch = 0.6f + speed / 14.f;
    aud.rollSurf = (float)surf;
    aud.grind = state == ST_GRIND ? 0.75f : 0.f;
    aud.grindMetal = (state == ST_GRIND && rail >= 0 && world.rails[rail].kind == RK_METAL) ? 1.f : 0.f;
    aud.wind = state == ST_AIR ? sat((len(vel) - 4.f) / 10.f) * 0.5f : sat((speed - 7.f) / 10.f) * 0.2f;
}

// ----------------------------------------------------------------------------
// Characters: an articulated box figure driven by a pose with 2-bone IK for
// arms and legs. Local character space faces -X, left side towards +Z.
// ----------------------------------------------------------------------------
static MeshBuilder DM;   // dynamic geometry, rebuilt every frame

struct Outfit {
    Col skin = hexc(0xc68a5a), shirt = hexc(0xc8302a), pants = hexc(0x2c3e66), shoes = hexc(0xe6e6e0);
    Col hat = hexc(0x1b1b1b), hair = hexc(0x2a1a10), bagCol = hexc(0x3a2a1a);
    int hat_ = 2;          // 0 none, 1 cap forward, 2 cap backwards, 3 beanie
    float height = 1.f;
    bool bag = false, longSleeves = false, baggy = true;
};
struct Pose {
    V3 pelvis = V3(0.03f, 0.88f, 0);
    float pelvisYaw = 0, lean = 0.15f, side = 0, twist = 0.35f;
    V3 footL = V3(0, 0, 0.24f), footR = V3(0.02f, 0, -0.25f);
    float footYawL = 0.25f, footYawR = 0.1f;
    V3 handL = V3(-0.05f, 0.62f, 0.4f), handR = V3(0.05f, 0.6f, -0.38f);
    V3 kneeHint = V3(-1, 0.1f, 0);
    float headYaw = 0.7f, headPitch = 0;
    V3 elbowHintL = V3(0.4f, -0.6f, 0.3f), elbowHintR = V3(0.4f, -0.6f, -0.3f);
};

static V3 ikJoint(V3 a, V3 target, float l1, float l2, V3 hint, V3& end) {
    V3 d = target - a;
    float L = len(d);
    V3 dn = L > 1e-5f ? d / L : V3(0, -1, 0);
    float Lc = clampf(L, 0.02f, l1 + l2 - 1e-3f);
    float x = (l1 * l1 - l2 * l2 + Lc * Lc) / (2 * Lc);
    float h = std::sqrt(std::max(l1 * l1 - x * x, 0.f));
    V3 hp = hint - dn * dot(hint, dn);
    if (len(hp) < 1e-4f) hp = std::fabs(dn.y) < 0.9f ? cross(dn, V3(0, 1, 0)) : cross(dn, V3(1, 0, 0));
    hp = norm(hp);
    end = a + dn * Lc;
    return a + dn * x + hp * h;
}

// Draw a figure. M maps character space to world. Pose values are in character space (metres, unscaled).
static void drawHuman(MeshBuilder& mb, const M4& M, const Pose& p, const Outfit& o) {
    float s = o.height;
    auto W = [&](V3 v) { return xPoint(M, v); };
    auto WD = [&](V3 v) { return xDir(M, v); };
    M4 Rp = mRotY(p.pelvisYaw);
    V3 left = xDir(Rp, V3(0, 0, 1));
    V3 pel = p.pelvis * s;
    M4 Rt = mRotY(p.pelvisYaw + p.twist) * mRotZ(p.lean) * mRotX(p.side);
    V3 tUp = xDir(Rt, V3(0, 1, 0)), tLeft = xDir(Rt, V3(0, 0, 1)), tFwd = xDir(Rt, V3(-1, 0, 0));
    float legW = o.baggy ? 0.2f : 0.15f;
    // legs
    for (int k = 0; k < 2; k++) {
        float sg = k == 0 ? 1.f : -1.f;
        V3 hip = pel + left * (0.1f * sg * s) - V3(0, 0.04f * s, 0);
        V3 foot = (k == 0 ? p.footL : p.footR) * s;
        V3 ankle = foot + V3(0, 0.08f * s, 0);
        V3 end;
        V3 hint = p.kneeHint + left * (0.25f * sg);
        V3 knee = ikJoint(hip, ankle, 0.44f * s, 0.44f * s, hint, end);
        mb.limb(W(hip), W(knee), legW * s, legW * s, WD(left), o.pants, MAT_CLOTH);
        mb.limb(W(knee), W(end), (legW - 0.02f) * s, (legW - 0.02f) * s, WD(left), o.pants, MAT_CLOTH);
        float fy = k == 0 ? p.footYawL : p.footYawR;
        V3 fdir(-std::cos(fy), 0, std::sin(fy));
        V3 fside = cross(V3(0, 1, 0), fdir);
        V3 fc = end + V3(0, -0.04f * s, 0) + fdir * (0.07f * s);
        mb.box(mBasis(WD(fside), WD(V3(0, 1, 0)), WD(fdir), W(fc)), V3(0.065f * s, 0.05f * s, 0.155f * s), o.shoes, MAT_CLOTH);
    }
    // hips + torso
    mb.box(mBasis(WD(left), WD(V3(0, 1, 0)), WD(xDir(Rp, V3(-1, 0, 0))), W(pel)), V3(0.19f * s, 0.1f * s, 0.13f * s), o.pants, MAT_CLOTH);
    V3 chestC = pel + tUp * (0.3f * s);
    mb.box(mBasis(WD(tLeft), WD(tUp), WD(tFwd), W(chestC)), V3(0.2f * s, 0.26f * s, 0.125f * s), o.shirt, MAT_CLOTH);
    V3 neck = pel + tUp * (0.56f * s);
    // head
    M4 Rh = Rt * mRotY(p.headYaw) * mRotZ(p.headPitch);
    V3 hUp = xDir(Rh, V3(0, 1, 0)), hLeft = xDir(Rh, V3(0, 0, 1)), hFwd = xDir(Rh, V3(-1, 0, 0));
    V3 headC = neck + hUp * (0.14f * s);
    mb.limb(W(neck - tUp * 0.02f), W(neck + hUp * 0.06f * s), 0.08f * s, 0.08f * s, WD(tLeft), o.skin, MAT_SKIN);
    M4 HB = mBasis(WD(hLeft), WD(hUp), WD(hFwd), W(headC));
    mb.box(HB, V3(0.1f * s, 0.12f * s, 0.11f * s), o.skin, MAT_SKIN);
    mb.box(HB * mTranslate(V3(0.035f * s, 0.025f * s, 0.112f * s)), V3(0.018f * s, 0.012f * s, 0.004f * s), hexc(0x151515), MAT_PLAIN);
    mb.box(HB * mTranslate(V3(-0.035f * s, 0.025f * s, 0.112f * s)), V3(0.018f * s, 0.012f * s, 0.004f * s), hexc(0x151515), MAT_PLAIN);
    if (o.hat_ == 0) {
        mb.box(HB * mTranslate(V3(0, 0.1f * s, -0.01f * s)), V3(0.105f * s, 0.035f * s, 0.115f * s), o.hair, MAT_CLOTH);
    } else if (o.hat_ == 3) {
        mb.box(HB * mTranslate(V3(0, 0.1f * s, -0.005f * s)), V3(0.11f * s, 0.06f * s, 0.12f * s), o.hat, MAT_CLOTH);
    } else {
        mb.box(HB * mTranslate(V3(0, 0.105f * s, 0)), V3(0.108f * s, 0.04f * s, 0.118f * s), o.hat, MAT_CLOTH);
        float bz = o.hat_ == 1 ? 0.16f : -0.16f;
        mb.box(HB * mTranslate(V3(0, 0.08f * s, bz * s)), V3(0.085f * s, 0.01f * s, 0.07f * s), o.hat, MAT_CLOTH);
    }
    // arms
    for (int k = 0; k < 2; k++) {
        float sg = k == 0 ? 1.f : -1.f;
        V3 sh = neck - tUp * (0.06f * s) + tLeft * (0.22f * sg * s);
        V3 hand = (k == 0 ? p.handL : p.handR) * s;
        V3 hint = k == 0 ? p.elbowHintL : p.elbowHintR;
        V3 end;
        V3 elbow = ikJoint(sh, hand, 0.29f * s, 0.27f * s, hint, end);
        mb.limb(W(sh), W(elbow), 0.12f * s, 0.12f * s, WD(tFwd), o.shirt, MAT_CLOTH);
        mb.limb(W(elbow), W(end), 0.085f * s, 0.085f * s, WD(tFwd), o.longSleeves ? o.shirt : o.skin, o.longSleeves ? MAT_CLOTH : MAT_SKIN);
        V3 hd = norm(end - elbow);
        mb.limb(W(end), W(end + hd * (0.09f * s)), 0.08f * s, 0.05f * s, WD(tFwd), o.skin, MAT_SKIN);
    }
    if (o.bag) {
        V3 bc = pel + left * (-0.26f * s) + V3(0, -0.1f * s, 0);
        mb.box(mBasis(WD(left), WD(V3(0, 1, 0)), WD(xDir(Rp, V3(-1, 0, 0))), W(bc)), V3(0.05f * s, 0.16f * s, 0.2f * s), o.bagCol, MAT_CLOTH);
    }
}

// Skateboard in board space: deck top centre at origin, nose +Z
static void drawBoard(MeshBuilder& mb, const M4& B) {
    Col grip = hexc(0x222222), deck = hexc(0xd8402a), truck = hexc(0xb8bcc0), wheel = hexc(0xf0ead8);
    mb.box(B * mTranslate(V3(0, -0.009f, 0)), V3(0.105f, 0.009f, 0.3f), deck, MAT_PAINTED, 63, &grip);
    for (int sg = -1; sg <= 1; sg += 2) {
        M4 K = B * mTranslate(V3(0, -0.009f, sg * 0.3f)) * mRotX(-sg * 0.33f) * mTranslate(V3(0, 0, sg * 0.075f));
        mb.box(K, V3(0.1f, 0.009f, 0.078f), deck, MAT_PAINTED, 63, &grip);
        mb.box(B * mTranslate(V3(0, -0.04f, sg * 0.22f)), V3(0.02f, 0.022f, 0.035f), truck, MAT_METAL);
        mb.box(B * mTranslate(V3(0, -0.062f, sg * 0.22f)), V3(0.085f, 0.012f, 0.014f), truck, MAT_METAL);
        for (int sx = -1; sx <= 1; sx += 2)
            mb.cylinder(B * mTranslate(V3(sx * 0.07f, -0.07f, sg * 0.22f)) * mRotZ(PI / 2), 0.028f, 0.032f, 8, wheel, MAT_PLAIN, true);
    }
    // stripe on the bottom
    mb.box(B * mTranslate(V3(0, -0.0185f, 0)), V3(0.02f, 0.0005f, 0.26f), hexc(0xf0e8d0), MAT_PLAIN, 8);
}

static Outfit PLAYER_OUTFIT;

// Build the skater's pose + board transform for the current state and draw them.
static void drawSkater(MeshBuilder& mb, const Player& pl, const Input& in) {
    if (pl.state == ST_BAIL) {
        // tumbling body + loose board
        V3 up(0, 1, 0), side = cross(up, fwdYaw(pl.yaw));
        M4 base = mBasis(side, up, fwdYaw(pl.yaw), pl.bodyPos + V3(0, 0.05f, 0));
        float ang = pl.bodyAng * (PI * 0.47f);
        M4 M = base * mRotZ(ang) * mTranslate(V3(0, -0.2f * pl.bodyAng, 0));
        Pose p;
        p.pelvis = V3(0.1f, 0.75f, 0);
        p.lean = -0.2f;
        p.handL = V3(-0.1f, 1.1f, 0.7f); p.handR = V3(0.1f, 1.05f, -0.7f);
        p.footL = V3(-0.1f, 0.1f, 0.35f); p.footR = V3(0.2f, 0.25f, -0.35f);
        p.headYaw = 0.2f; p.headPitch = 0.3f;
        drawHuman(mb, M, p, PLAYER_OUTFIT);
        M4 B = mTranslate(pl.boardPos + V3(0, 0.08f, 0)) * mRotY(pl.yaw + pl.boardAng * 0.5f) * mRotZ(pl.boardAng);
        drawBoard(mb, B);
        return;
    }
    V3 up = norm(pl.upVis);
    V3 f = fwdYaw(pl.yaw);
    V3 fwd = f - up * dot(f, up);
    if (len(fwd) < 1e-3f) fwd = V3(0, 0, 1);
    fwd = norm(fwd);
    V3 side = cross(up, fwd);
    float lift = 0.1f;
    if (pl.state == ST_GRIND) lift = GRINDS[pl.grindIdx].slide ? 0.03f : 0.08f;
    M4 R = mBasis(side, up, fwd, pl.pos + up * lift);   // rider space == board space (no trick rotation)
    Pose p;
    float speed = len(pl.vel);
    bool fakie = pl.isFakie();
    p.headYaw = fakie ? -0.9f : 0.75f;
    float steer = (in.left ? 1.f : 0.f) - (in.right ? 1.f : 0.f);
    float squash = pl.landSquash;
    M4 boardT = M4::ident();   // trick transform applied to the board in rider space
    V3 boardOff(0, 0, 0);
    if (pl.state == ST_RIDE || pl.state == ST_MANUAL) {
        float crouch = pl.crouching ? smooth01(pl.crouchT / 0.3f) : 0.f;
        p.pelvis.y = 0.86f - crouch * 0.26f - squash * 0.2f - (pl.braking ? 0.08f : 0.f);
        p.lean = 0.15f + crouch * 0.25f - steer * 0.22f * sat(speed / 6.f);
        p.side = 0;
        if (crouch > 0) { p.handL = V3(-0.25f, 0.45f, 0.32f); p.handR = V3(-0.2f, 0.42f, -0.3f); }
        if (pl.braking) {   // heel drag / powerslide stance
            p.footR = V3(0.05f, 0.0f, -0.28f);
            p.lean = -0.05f;
            boardT = mRotY(0.25f);
        }
        if (pl.pushing || pl.pushPhase > 0) {
            float ph = std::fmod(pl.pushPhase, 1.f);
            float gy = -0.1f;
            V3 onBoard(0.02f, 0, -0.25f);
            V3 plant(-0.16f, gy, 0.12f), pushEnd(-0.16f, gy, -0.6f);
            V3 fp;
            if (ph < 0.25f) fp = lerp3(onBoard, plant, smooth01(ph / 0.25f)) + V3(0, std::sin(ph / 0.25f * PI) * 0.08f, 0);
            else if (ph < 0.65f) fp = lerp3(plant, pushEnd, (ph - 0.25f) / 0.4f);
            else fp = lerp3(pushEnd, onBoard, smooth01((ph - 0.65f) / 0.35f)) + V3(0, std::sin((ph - 0.65f) / 0.35f * PI) * 0.12f, 0);
            p.footR = fp;
            p.footYawL = 1.35f;
            p.footYawR = 1.2f;
            p.twist = 1.1f;
            p.headYaw = 0.35f;
            p.pelvis = V3(0.0f, 0.8f, 0.1f);
            p.lean = 0.3f;
            p.handL = V3(-0.1f, 0.62f, 0.5f);
            p.handR = V3(0.15f, 0.62f, -0.3f);
        }
        if (pl.state == ST_MANUAL) {
            float a = pl.nose ? 0.2f : -0.2f;
            float pivot = pl.nose ? 0.22f : -0.22f;
            boardT = mTranslate(V3(0, 0, pivot)) * mRotX(a) * mTranslate(V3(0, 0, -pivot));
            if (!pl.nose) { p.footL = V3(0, 0.09f, 0.24f); p.footR = V3(0.02f, 0.0f, -0.24f); }
            else { p.footL = V3(0, 0.0f, 0.24f); p.footR = V3(0.02f, 0.09f, -0.24f); }
            p.pelvis = V3(0.04f, 0.8f, pl.nose ? 0.1f : -0.1f);
            p.side = (pl.nose ? 0.15f : -0.15f) + pl.bal * 0.25f;
            p.handL = V3(0.0f, 0.95f, 0.65f);
            p.handR = V3(0.05f, 0.9f, -0.65f);
        }
    } else if (pl.state == ST_AIR) {
        float t = pl.airT;
        float tuck = smooth01(t / 0.18f) * (1.f - smooth01((-pl.vel.y - 3.5f) / 3.f) * 0.6f);
        p.pelvis.y = 0.86f - tuck * 0.22f;
        p.lean = 0.25f;
        p.handL = V3(-0.1f, 0.95f, 0.55f);
        p.handR = V3(0.05f, 0.9f, -0.55f);
        p.elbowHintL = V3(0.2f, -0.5f, 0.6f);
        p.elbowHintR = V3(0.2f, -0.5f, -0.6f);
        if (pl.flipIdx >= 0) {
            const FlipDef& fd = FLIPS[pl.flipIdx];
            float u = sat(pl.flipT / pl.flipDur);
            float e = u * u * (3 - 2 * u);
            float turns = 1.f + pl.flipExtra;
            float fl = fd.flip * turns * TAU * e, sh = fd.shove * TAU * e * (1 + pl.flipExtra * 0.0f), im = fd.imp * turns * TAU * e;
            boardOff = V3(0, 0.12f * std::sin(u * PI) - 0.05f, 0);
            boardT = mTranslate(V3(0, -0.04f, 0)) * mRotY(sh) * mRotZ(fl) * mRotX(im) * mTranslate(V3(0, 0.04f, 0));
            float lift = std::sin(u * PI) * 0.22f;
            p.footL.y += lift; p.footR.y += lift * 0.9f;
            p.footL.x -= lift * 0.3f;
        }
        if (pl.grabIdx >= 0) {
            float g = smooth01(pl.grabBlend);
            p.pelvis.y = 0.86f - 0.3f * g;
            p.lean = 0.25f + 0.25f * g;
            boardOff = V3(0, 0.2f * g, 0);
            V3 target;
            bool front = true;
            switch (pl.grabIdx) {
                case 0: target = V3(-0.1f, 0, -0.04f); front = false; break;                      // indy: toe edge, back hand
                case 1: target = V3(0.1f, 0, 0.05f); break;                                         // melon: heel edge, front hand
                case 2: target = V3(0.1f, 0, 0.04f); boardT = mTranslate(V3(0.18f * g, 0.1f * g, 0)) * mRotZ(-0.6f * g); break;  // method
                case 3: target = V3(0, 0, 0.4f); break;                                             // nosegrab
                case 4: target = V3(0, 0, -0.4f); front = false; break;                             // tailgrab
                case 5: target = V3(0, 0, 0.38f); p.footR = V3(0.25f, -0.35f * g, -0.45f); break;   // madonna
                case 6: target = V3(0, 0, -0.38f); front = false; p.footR = V3(-0.15f, -0.4f * g, -0.5f); break;  // benihana
                case 7: target = V3(0.1f, 0, -0.14f); front = false; break;                         // stalefish
                case 8: target = V3(0.1f, 0, 0.08f); boardT = mRotX(0.4f * g); break;               // crossbone
            }
            V3 tw = xPoint(mTranslate(boardOff) * boardT, target) + V3(0, 0.02f, 0);
            if (front) { p.handL = lerp3(p.handL, tw, g); p.elbowHintL = V3(-0.3f, 0.2f, 0.5f); }
            else { p.handR = lerp3(p.handR, tw, g); p.elbowHintR = V3(-0.3f, 0.2f, -0.5f); }
            if (pl.grabIdx != 5 && pl.grabIdx != 6) { p.footL.y += boardOff.y; p.footR.y += boardOff.y; }
            else p.footL.y += boardOff.y;
        }
        if (pl.qpAir) p.headYaw = 0.2f;
    } else if (pl.state == ST_GRIND) {
        const GrindDef& gd = GRINDS[pl.grindIdx];
        float pv = gd.pitch > 0 ? -0.22f : (gd.pitch < 0 ? 0.22f : 0.f);   // pivot on the grinding truck
        boardT = mTranslate(V3(0, 0, pv)) * mRotX(-gd.pitch) * mRotZ(gd.roll) * mTranslate(V3(0, 0, -pv));
        p.pelvis = V3(0.02f, 0.8f, 0);
        p.lean = 0.12f;
        p.side = pl.bal * 0.35f;
        p.handL = V3(-0.05f, 1.0f, 0.72f);
        p.handR = V3(0.08f, 0.95f, -0.72f);
        p.elbowHintL = V3(0.1f, -0.6f, 0.3f);
        p.elbowHintR = V3(0.1f, -0.6f, -0.3f);
        if (gd.slide) { p.twist = 0.9f; p.headYaw = 0.5f; }
        if (gd.pitch != 0) {
            float pd = 0.3f * std::sin(gd.pitch);
            p.footL.y = -pd; p.footR.y = pd;
        }
    }
    M4 B = R * mTranslate(boardOff) * boardT;
    drawBoard(mb, B);
    drawHuman(mb, R, p, PLAYER_OUTFIT);
}

// ----------------------------------------------------------------------------
// Living city: pedestrians, pigeons, traffic, signals, collectible letters
// ----------------------------------------------------------------------------
struct Bubble { V3 pos; std::string text; float t; };
static std::vector<Bubble> bubbles;
static void speech(V3 p, const std::string& s) {
    for (auto& b : bubbles) if (len(b.pos - p) < 0.5f) { b.text = s; b.t = 2.2f; return; }
    bubbles.push_back({p, s, 2.2f});
}

struct PathInfo { std::vector<float> cum; float total = 0; };
static std::vector<PathInfo> pathInfo;
static void preparePaths() {
    pathInfo.clear();
    for (auto& np : npcPaths) {
        PathInfo pi;
        pi.cum.push_back(0);
        size_t n = np.pts.size();
        size_t segs = np.loop ? n : n - 1;
        for (size_t i = 0; i < segs; i++) {
            V3 a = np.pts[i], b = np.pts[(i + 1) % n];
            pi.total += lenXZ(b - a);
            pi.cum.push_back(pi.total);
        }
        pathInfo.push_back(pi);
    }
}
static void pathSample(int path, float s, V3& p, V3& dir) {
    const NpcPath& np = npcPaths[path];
    const PathInfo& pi = pathInfo[path];
    size_t n = np.pts.size();
    size_t segs = pi.cum.size() - 1;
    size_t i = 0;
    while (i + 1 < segs && pi.cum[i + 1] < s) i++;
    V3 a = np.pts[i], b = np.pts[(i + 1) % n];
    float L = std::max(1e-3f, pi.cum[i + 1] - pi.cum[i]);
    float t = clampf((s - pi.cum[i]) / L, 0, 1);
    p = lerp3(a, b, t);
    dir = norm(V3(b.x - a.x, 0, b.z - a.z));
}

struct Npc {
    int path = 0, dir = 1;
    float s = 0, speed = 1.3f, lateral = 0, targetLat = 0, phase = 0;
    V3 pos;
    float yaw = 0;
    Outfit outfit;
    float knocked = 0, cheerCool = 0, dodgeCool = 0;
};
static std::vector<Npc> npcs;

static void initNpcs() {
    preparePaths();
    Rng r(9001);
    static const uint32_t skins[] = {0xf1c7a5, 0xd9a47a, 0xc68a5a, 0x9c6a42, 0x6e4a2e, 0x4a3020};
    static const uint32_t shirts[] = {0x2f5d9e, 0xd8d4c8, 0x8e2a2a, 0x2e6b3a, 0xe0b030, 0x111111, 0x6a4a8a, 0xe07a30, 0x607080, 0xf0f0f0};
    static const uint32_t pants[] = {0x1f2a44, 0x222222, 0x5a4a3a, 0x6a6a6a, 0x2a3a5a, 0xb0a080};
    for (int p = 0; p < (int)npcPaths.size(); p++) {
        int count = std::max(1, (int)(pathInfo[p].total / 22.f));
        count = std::min(count, 4);
        for (int k = 0; k < count; k++) {
            Npc n;
            n.path = p;
            n.s = r.range(0, pathInfo[p].total);
            n.dir = r.chance(0.5f) ? 1 : -1;
            n.speed = r.range(1.0f, 1.6f);
            n.lateral = n.targetLat = r.range(-0.3f, 0.3f);
            n.phase = r.range(0, TAU);
            Outfit& o = n.outfit;
            o.skin = hexc(skins[r.irange(0, 5)]);
            o.shirt = hexc(shirts[r.irange(0, 9)]);
            o.pants = hexc(pants[r.irange(0, 5)]);
            o.shoes = r.chance(0.5f) ? hexc(0x1a1a1a) : hexc(0xdedad0);
            o.hat_ = r.chance(0.35f) ? (r.chance(0.5f) ? 1 : 3) : 0;
            o.hat = r.chance(0.5f) ? hexc(0x1c2a4a) : hexc(0x8a1a1a);
            o.hair = r.chance(0.5f) ? hexc(0x1a120c) : hexc(0x6a4a2a);
            o.height = r.range(0.9f, 1.07f);
            o.bag = r.chance(0.35f);
            o.bagCol = r.chance(0.5f) ? hexc(0x3a2a1a) : hexc(0x1a1a1a);
            o.longSleeves = r.chance(0.5f);
            o.baggy = r.chance(0.3f);
            npcs.push_back(n);
        }
    }
}

static const char* CHEERS[] = {"NICE!", "SICK!", "WHOA!", "YO! DO THAT AGAIN!", "THAT WAS DOPE!", "RESPECT!"};
static const char* ANGRY[] = {"HEY! I'M WALKIN' HERE!", "WATCH IT, KID!", "GET OFF THE SIDEWALK!", "OW! MY COFFEE!"};

static void updateNpcs(float dt, Player& pl, long long& lastBankSeen) {
    static Rng r(4242);
    bool cheerEvent = false;
    if (pl.score != lastBankSeen) { cheerEvent = pl.score - lastBankSeen >= 800; lastBankSeen = pl.score; }
    if (cheerEvent) {   // the closest onlooker always reacts to a big line
        Npc* best = nullptr;
        float bd = 18.f;
        for (auto& n : npcs)
            if (n.knocked <= 0 && n.cheerCool <= 0 && len(n.pos - pl.pos) < bd) { bd = len(n.pos - pl.pos); best = &n; }
        if (best) { speech(best->pos + V3(0, 2.1f, 0), CHEERS[r.irange(0, 5)]); best->cheerCool = 6.f; }
        cheerEvent = false;
    }
    for (auto& n : npcs) {
        n.cheerCool = std::max(0.f, n.cheerCool - dt);
        n.dodgeCool = std::max(0.f, n.dodgeCool - dt);
        if (n.knocked > 0) { n.knocked -= dt; continue; }
        const PathInfo& pi = pathInfo[n.path];
        bool loop = npcPaths[n.path].loop;
        n.s += n.dir * n.speed * dt;
        if (loop) { if (n.s > pi.total) n.s -= pi.total; if (n.s < 0) n.s += pi.total; }
        else if (n.s > pi.total) { n.s = pi.total; n.dir = -1; }
        else if (n.s < 0) { n.s = 0; n.dir = 1; }
        V3 p, d;
        pathSample(n.path, n.s, p, d);
        V3 wd = d * (float)n.dir;
        V3 perp = cross(V3(0, 1, 0), wd);
        n.lateral = damp(n.lateral, n.targetLat, 3.f, dt);
        V3 np = p + perp * n.lateral;
        np.y = world.ground(np.x, np.z, p.y + 0.4f).h;
        n.pos = np;
        float ty = yawOf(wd);
        n.yaw = n.yaw + wrapPi(ty - n.yaw) * std::min(1.f, dt * 6.f);
        n.phase += n.speed * dt * 4.2f;
        // dodge a skater that is about to run us over
        if (pl.state != ST_BAIL && n.dodgeCool <= 0) {
            V3 rel = n.pos - pl.pos;
            V3 pv(pl.vel.x, 0, pl.vel.z);
            float sp = len(pv);
            if (sp > 3.f && std::fabs(rel.y) < 1.2f) {
                float tca = dot(V3(rel.x, 0, rel.z), pv) / (sp * sp);
                if (tca > 0 && tca < 0.7f) {
                    V3 closest = V3(pl.pos.x, 0, pl.pos.z) + pv * tca;
                    V3 miss = V3(n.pos.x, 0, n.pos.z) - closest;
                    if (len(miss) < 0.9f) {
                        float sideSign = dot(miss, perp) >= 0 ? 1.f : -1.f;
                        n.targetLat = clampf(n.lateral + sideSign * 1.3f, -1.8f, 1.8f);
                        n.dodgeCool = 1.2f;
                        if (r.chance(0.3f)) speech(n.pos + V3(0, 2.1f, 0), "WHOA!");
                    }
                }
            }
        }
        if (n.dodgeCool <= 0 && std::fabs(n.targetLat) > 0.6f) n.targetLat *= 0.5f;
        // collisions with the skater
        if (pl.state != ST_BAIL) {
            V3 rel = pl.pos - n.pos;
            float dh = std::sqrt(rel.x * rel.x + rel.z * rel.z);
            if (dh < 0.55f && rel.y > -0.5f && rel.y < 1.5f) {
                float relSpeed = len(V3(pl.vel.x, 0, pl.vel.z) - wd * n.speed);
                if (relSpeed > 3.2f) {
                    n.knocked = 2.4f;
                    speech(n.pos + V3(0, 2.1f, 0), ANGRY[r.irange(0, 3)]);
                    sfx(SFX_HEY, 0.8f, r.range(0.9f, 1.2f));
                    pl.bail("RAN INTO A PEDESTRIAN");
                } else if (dh > 1e-3f) {
                    V3 push = V3(rel.x, 0, rel.z) / dh * (0.55f - dh);
                    pl.pos += push;
                }
            }
        }
    }
}

static void walkPose(Pose& p, float phase, float speed) {
    float sw = std::sin(phase), cw = std::cos(phase);
    float stride = 0.14f + speed * 0.1f;
    p.pelvis = V3(0, 0.93f + 0.025f * std::fabs(std::sin(phase)), 0);
    p.lean = 0.06f;
    p.twist = 0;
    p.side = 0;
    p.footL = V3(-sw * stride, std::max(0.f, cw) * 0.1f, 0.1f);
    p.footR = V3(sw * stride, std::max(0.f, -cw) * 0.1f, -0.1f);
    p.footYawL = p.footYawR = 0;
    p.handL = V3(sw * 0.2f + 0.02f, 0.5f, 0.3f);
    p.handR = V3(-sw * 0.2f + 0.02f, 0.5f, -0.3f);
    p.kneeHint = V3(-1, 0, 0);
    p.elbowHintL = V3(0.6f, -0.4f, 0.2f);
    p.elbowHintR = V3(0.6f, -0.4f, -0.2f);
    p.headYaw = 0;
}

static void drawNpcs(MeshBuilder& mb, V3 cam) {
    for (auto& n : npcs) {
        if (len(n.pos - cam) > 90.f) continue;
        Pose p;
        M4 M = mTranslate(n.pos) * mRotY(n.yaw + PI / 2);
        if (n.knocked > 0) {
            float k = std::min(1.f, (2.4f - n.knocked) * 4.f);
            M = M * mRotZ(-k * PI * 0.45f);
            walkPose(p, 0, 0);
            p.handL = V3(-0.3f, 1.2f, 0.5f); p.handR = V3(-0.3f, 1.2f, -0.5f);
        } else walkPose(p, n.phase, n.speed);
        drawHuman(mb, M, p, n.outfit);
    }
}

// ---------------------------------------------------------------- pigeons
struct Pigeon { V3 home, pos, vel; float yaw = 0, t = 0, flap = 0, peck = 0; int state = 0; };
static std::vector<Pigeon> pigeons;
static void initPigeons() {
    Rng r(31337);
    for (V3 s : pigeonSpots)
        for (int i = 0; i < 7; i++) {
            Pigeon p;
            p.home = s + V3(r.range(-1.6f, 1.6f), 0, r.range(-1.6f, 1.6f));
            p.home.y = world.ground(p.home.x, p.home.z, s.y + 0.5f).h;
            p.pos = p.home;
            p.yaw = r.range(0, TAU);
            p.peck = r.range(0, 10);
            pigeons.push_back(p);
        }
}
static void updatePigeons(float dt, const Player& pl) {
    static Rng r(99);
    bool flock = false;
    for (auto& p : pigeons) {
        p.t += dt;
        if (p.state == 0) {
            p.peck += dt;
            if (r.chance(dt * 0.4f)) p.yaw += r.range(-1.5f, 1.5f);
            V3 rel = p.pos - pl.pos;
            float spd = len(pl.vel);
            if (len(rel) < 3.8f && (spd > 1.2f || pl.state == ST_AIR)) {
                p.state = 1;
                p.t = 0;
                V3 away = norm(V3(rel.x, 0, rel.z) + V3(r.range(-0.5f, 0.5f), 0, r.range(-0.5f, 0.5f)));
                p.vel = away * r.range(3.f, 5.f) + V3(0, r.range(3.5f, 5.5f), 0);
                p.yaw = yawOf(away);
                flock = true;
            }
        } else if (p.state == 1) {
            p.vel.y += (1.2f - p.vel.y) * dt * 0.8f;
            p.pos += p.vel * dt;
            p.flap += dt * 22.f;
            if (p.t > 4.f) { p.state = 2; p.t = 0; }
        } else {
            // return home when the skater is away
            if (p.t > 6.f && len(p.home - pl.pos) > 15.f) { p.state = 0; p.pos = p.home; p.t = 0; }
        }
    }
    if (flock) sfx(SFX_PIGEONS, 0.7f);
}
static void drawPigeons(MeshBuilder& mb, V3 cam) {
    Col body = hexc(0x7d8088), head = hexc(0x4f5a5e), wing = hexc(0x8f939a);
    for (auto& p : pigeons) {
        if (p.state == 2 || len(p.pos - cam) > 70.f) continue;
        M4 F = mTranslate(p.pos) * mRotY(p.yaw);
        float bob = p.state == 0 ? std::max(0.f, std::sin(p.peck * 5.f)) * 0.05f : 0;
        mb.box(F * mTranslate(V3(0, 0.1f, 0)), V3(0.055f, 0.055f, 0.11f), body, MAT_CLOTH);
        mb.box(F * mTranslate(V3(0, 0.17f - bob, 0.1f + bob * 0.5f)), V3(0.035f, 0.035f, 0.04f), head, MAT_CLOTH);
        mb.box(F * mTranslate(V3(0, 0.1f, -0.14f)), V3(0.04f, 0.012f, 0.05f), head, MAT_CLOTH);
        if (p.state == 1) {
            float a = std::sin(p.flap) * 0.9f;
            for (int s = -1; s <= 1; s += 2)
                mb.box(F * mTranslate(V3(s * 0.05f, 0.12f, 0)) * mRotZ(s * a) * mTranslate(V3(s * 0.13f, 0, 0)), V3(0.13f, 0.008f, 0.07f), wing, MAT_CLOTH);
        } else {
            for (int s = -1; s <= 1; s += 2) mb.box(F * mTranslate(V3(s * 0.05f, 0.11f, -0.02f)), V3(0.012f, 0.04f, 0.09f), wing, MAT_CLOTH);
        }
    }
}

// ---------------------------------------------------------------- traffic
struct Lane { float z; int dir; bool hasLight; };
static const Lane LANES[4] = {{-2.1f, 1, true}, {2.1f, 1, true}, {-71.0f, -1, false}, {-66.4f, 1, false}};
struct Car { int lane; float x, speed, target; int type; Col col; float honk = 0, brake = 0; };
static std::vector<Car> cars;
static float tlTimer = 0;
static int ewPhase() {   // 0 green, 1 yellow, 2 red for east-west traffic
    float t = std::fmod(tlTimer, 32.f);
    if (t < 13) return 0;
    if (t < 16) return 1;
    return 2;
}
static int nsPhase() {
    float t = std::fmod(tlTimer, 32.f);
    if (t >= 17 && t < 29) return 0;
    if (t >= 29 && t < 31) return 1;
    return 2;
}
static void initTraffic() {
    Rng r(555);
    Col cols[] = {hexc(0xf2c318), hexc(0xf2c318), hexc(0xf2c318), hexc(0x1b1b1d), hexc(0x7a1f22), hexc(0x9ca3a8), hexc(0x274a78), hexc(0xd8d4c8)};
    for (int l = 0; l < 4; l++)
        for (int k = 0; k < 3; k++) {
            Car c;
            c.lane = l;
            c.x = -120.f + k * 80.f + r.range(-10, 10);
            c.speed = c.target = r.range(8.f, 11.f);
            c.col = cols[r.irange(0, 7)];
            c.type = c.col.r == 0xf2 ? 0 : r.irange(1, 3);
            cars.push_back(c);
        }
}
static void updateTraffic(float dt, Player& pl) {
    tlTimer += dt;
    for (auto& c : cars) {
        const Lane& L = LANES[c.lane];
        float want = c.target;
        // car ahead
        for (auto& o : cars) {
            if (&o == &c || o.lane != c.lane) continue;
            float gap = (o.x - c.x) * L.dir;
            if (gap > 0 && gap < 9.f) want = std::min(want, std::max(0.f, (gap - 5.5f) * 1.5f));
        }
        // red light (stop line west of the intersection for eastbound traffic)
        if (L.hasLight && ewPhase() != 0) {
            float stopX = -14.5f;
            float gap = (stopX - c.x) * L.dir;
            if (gap > 0 && gap < 22.f && (ewPhase() == 2 || gap > 6.f)) want = std::min(want, std::max(0.f, (gap - 0.5f) * 0.9f));
        }
        // the skater in the lane ahead
        if (pl.state != ST_BAIL && pl.pos.y < 1.2f && std::fabs(pl.pos.z - L.z) < 1.9f) {
            float gap = (pl.pos.x - c.x) * L.dir;
            if (gap > 0 && gap < 11.f) {
                want = std::min(want, std::max(0.f, (gap - 3.4f) * 1.4f));
                if (c.honk <= 0 && std::fabs(pl.pos.x - c.x) < 11.f) { sfx(SFX_HONK, 0.8f, c.type == 0 ? 1.1f : 0.9f); c.honk = 2.5f; }
            }
        }
        c.honk = std::max(0.f, c.honk - dt);
        float acc = want > c.speed ? 3.f : 9.f;
        c.brake = want < c.speed - 0.2f ? 1.f : 0.f;
        c.speed = approach(c.speed, want, acc * dt);
        c.x += c.speed * L.dir * dt;
        if (c.x * L.dir > 140.f) c.x = -140.f * L.dir;
        // hit the skater?
        if (pl.state != ST_BAIL && pl.pos.y < 1.6f) {
            float dx = std::fabs(pl.pos.x - c.x), dz = std::fabs(pl.pos.z - L.z);
            if (dx < 2.6f && dz < 1.2f && c.speed > 2.f) {
                pl.vel += V3((float)L.dir * c.speed * 0.6f, 2.f, 0);
                pl.bail(c.type == 0 ? "HIT BY A CAB!" : "HIT BY A CAR!");
                sfx(SFX_HONK, 1.f);
            } else if (dx < 2.5f && dz < 1.1f) {
                pl.pos.z = L.z + (pl.pos.z > L.z ? 1.25f : -1.25f);
            }
        }
    }
}
static void drawTraffic(MeshBuilder& mb, V3 cam) {
    for (auto& c : cars) {
        const Lane& L = LANES[c.lane];
        if (std::fabs(c.x - cam.x) > 150) continue;
        carGeom(mb, frame(c.x, 0, L.z, L.dir > 0 ? PI / 2 : -PI / 2), c.type, c.col, c.brake);
    }
}
static void drawSignals(MeshBuilder& mb) {
    for (auto& t : tlights) {
        int ph = t.axis == 0 ? ewPhase() : nsPhase();
        M4 F = mTranslate(t.pos) * mRotY(t.yaw);
        mb.box(F * mTranslate(V3(0, 0, 0.2f)), V3(0.16f, 0.45f, 0.12f), hexc(0x2a2e1e), MAT_PAINTED);
        Col lamp[3] = {hexc(0x3a0c08), hexc(0x3a2c08), hexc(0x0a3010)};
        if (ph == 2) lamp[0] = hexc(0xff3020);
        if (ph == 1) lamp[1] = hexc(0xffb020);
        if (ph == 0) lamp[2] = hexc(0x30ff70);
        for (int i = 0; i < 3; i++)
            mb.box(F * mTranslate(V3(0, 0.28f - i * 0.28f, 0.325f)), V3(0.09f, 0.09f, 0.01f), lamp[i], MAT_EMISSIVE);
    }
}
static void drawLetters(MeshBuilder& mb, const Player& pl, float time) {
    static const char L[5] = {'S', 'K', 'A', 'T', 'E'};
    for (int i = 0; i < 5 && i < (int)letterPos.size(); i++) {
        if (pl.letters[i]) continue;
        V3 c = letterPos[i] + V3(0, 0.12f * std::sin(time * 2.f + i), 0);
        float a = time * 1.6f + i;
        V3 right(std::cos(a), 0, -std::sin(a)), up(0, 1, 0);
        float px = 0.13f;
        V3 o = c - right * (2.5f * px) - up * (3.5f * px);
        mb.text3D(std::string(1, L[i]), o, right, up, px, hexc(0xffd23a), MAT_EMISSIVE, 0.08f);
        mb.text3D(std::string(1, L[i]), o + right * (5 * px) + cross(right, up) * 0.08f, right * -1.f, up, px, hexc(0xffd23a), MAT_EMISSIVE, 0.0f);
    }
}

// ----------------------------------------------------------------------------
// Particles: fountain jets, hydrant spray, manhole steam, sparks, splashes, dust
// ----------------------------------------------------------------------------
struct Particle {
    V3 p, v;
    float life = 1, maxLife = 1, size = 0.1f, grow = 0, drag = 0, grav = 9.8f;
    float r = 1, g = 1, b = 1, a = 1;
    bool additive = false, water = false;
};
static std::vector<Particle> parts;
static Rng prng(2024);
static void addParticle(const Particle& p) { if (parts.size() < 7000) parts.push_back(p); }
static V3 randDir() {
    for (;;) {
        V3 d(prng.range(-1, 1), prng.range(-1, 1), prng.range(-1, 1));
        float l = len(d);
        if (l > 0.05f && l <= 1.f) return d / l;
    }
}
static void spawnDust(V3 p, int n) {
    for (int i = 0; i < n; i++) {
        Particle q;
        q.p = p + V3(prng.range(-0.3f, 0.3f), 0.05f, prng.range(-0.3f, 0.3f));
        q.v = V3(prng.range(-1.2f, 1.2f), prng.range(0.3f, 1.2f), prng.range(-1.2f, 1.2f));
        q.life = q.maxLife = prng.range(0.5f, 0.9f);
        q.size = 0.12f; q.grow = 0.5f; q.drag = 3.f; q.grav = 0.5f;
        q.r = 0.62f; q.g = 0.58f; q.b = 0.52f; q.a = 0.35f;
        addParticle(q);
    }
}
static void spawnSparks(V3 p, V3 v, int n) {
    for (int i = 0; i < n; i++) {
        Particle q;
        q.p = p + V3(0, 0.02f, 0);
        q.v = v * -0.25f + randDir() * prng.range(1.f, 3.5f) + V3(0, 1.2f, 0);
        q.life = q.maxLife = prng.range(0.18f, 0.45f);
        q.size = 0.035f; q.grav = 9.8f; q.drag = 1.f;
        q.r = 1.f; q.g = prng.range(0.55f, 0.85f); q.b = 0.25f; q.a = 1.f;
        q.additive = true;
        addParticle(q);
    }
}
static void spawnSplash(V3 p, int n, float power) {
    for (int i = 0; i < n; i++) {
        Particle q;
        q.p = p + V3(prng.range(-0.4f, 0.4f), 0.1f, prng.range(-0.4f, 0.4f));
        V3 d = randDir();
        q.v = V3(d.x * 2.5f, std::fabs(d.y) * 5.f + 1.5f, d.z * 2.5f) * power;
        q.life = q.maxLife = prng.range(0.6f, 1.3f);
        q.size = prng.range(0.05f, 0.12f); q.grav = 9.8f; q.drag = 0.5f;
        q.r = 0.85f; q.g = 0.93f; q.b = 1.f; q.a = 0.7f;
        q.water = true;
        addParticle(q);
    }
}

static void updateParticles(float dt, V3 cam) {
    // emitters near the camera
    for (auto& e : emitters) {
        if (len(e.pos - cam) > 75.f) continue;
        e.acc += e.rate * dt;
        while (e.acc >= 1.f) {
            e.acc -= 1.f;
            Particle q;
            q.water = true;
            switch (e.kind) {
                case EM_FOUNTAIN: {
                    float a = prng.range(0, TAU), r = prng.range(0.f, 0.55f);
                    q.p = e.pos;
                    q.v = V3(std::sin(a) * r, prng.range(5.2f, 6.4f), std::cos(a) * r);
                    q.life = q.maxLife = 2.2f;
                    q.size = prng.range(0.05f, 0.09f); q.grav = 9.8f;
                    q.r = 0.82f; q.g = 0.92f; q.b = 1.f; q.a = 0.55f;
                    break;
                }
                case EM_POOLSPLASH: {
                    float a = prng.range(0, TAU);
                    V3 o(std::sin(a), 0, std::cos(a));
                    q.p = e.pos + o * 1.9f;
                    q.v = o * prng.range(0.3f, 0.9f) + V3(0, prng.range(-0.5f, 0.2f), 0);
                    q.life = q.maxLife = 1.2f;
                    q.size = prng.range(0.04f, 0.07f); q.grav = 9.8f;
                    q.r = 0.8f; q.g = 0.9f; q.b = 1.f; q.a = 0.5f;
                    break;
                }
                case EM_HYDRANT: {
                    V3 side = cross(V3(0, 1, 0), e.dir);
                    q.p = e.pos;
                    q.v = e.dir * prng.range(8.f, 11.f) + V3(0, prng.range(1.6f, 3.2f), 0) + side * prng.range(-1.1f, 1.1f);
                    q.life = q.maxLife = 1.6f;
                    q.size = prng.range(0.05f, 0.11f); q.grav = 9.8f; q.drag = 0.4f;
                    q.r = 0.85f; q.g = 0.93f; q.b = 1.f; q.a = 0.6f;
                    if (prng.chance(0.15f)) { q.size = 0.35f; q.grow = 0.8f; q.a = 0.12f; q.water = false; q.grav = 2.f; q.drag = 1.5f; }
                    break;
                }
                case EM_STEAM: {
                    q.p = e.pos + V3(prng.range(-0.3f, 0.3f), 0, prng.range(-0.3f, 0.3f));
                    q.v = V3(prng.range(-0.2f, 0.2f) + 0.25f, prng.range(0.7f, 1.3f), prng.range(-0.2f, 0.2f) + 0.1f);
                    q.life = q.maxLife = prng.range(2.5f, 3.5f);
                    q.size = 0.35f; q.grow = 0.55f; q.grav = -0.1f; q.drag = 0.3f;
                    q.r = q.g = q.b = 0.92f; q.a = 0.2f;
                    q.water = false;
                    break;
                }
            }
            addParticle(q);
        }
    }
    static std::vector<Particle> spawned;   // droplets born this frame (appended after the loop:
    spawned.clear();                        // pushing into 'parts' here would invalidate 'q')
    for (size_t i = 0; i < parts.size();) {
        Particle& q = parts[i];
        q.life -= dt;
        q.v.y -= q.grav * dt;
        q.v = q.v * std::exp(-q.drag * dt);
        q.p += q.v * dt;
        q.size += q.grow * dt;
        bool dead = q.life <= 0;
        if (q.water && q.v.y < 0) {
            float gy = world.ground(q.p.x, q.p.z, q.p.y + 0.3f).h;
            // fountain pool surface sits a little above its floor
            for (auto& pool : world.pools)
                if ((q.p.x - pool.x) * (q.p.x - pool.x) + (q.p.z - pool.z) * (q.p.z - pool.z) < pool.r * pool.r) gy = std::max(gy, SH + 0.38f);
            if (q.p.y < gy) {
                dead = true;
                if (prng.chance(0.08f)) {   // little bounce droplet
                    Particle s = q;
                    s.p.y = gy + 0.02f; s.v = V3(prng.range(-0.6f, 0.6f), prng.range(0.6f, 1.4f), prng.range(-0.6f, 0.6f));
                    s.life = s.maxLife = 0.3f; s.size *= 0.7f; s.water = false;
                    spawned.push_back(s);
                }
            }
        }
        if (dead) { q = parts.back(); parts.pop_back(); continue; }
        i++;
    }
    for (const Particle& s : spawned) addParticle(s);
}

// Particle vertex: pos3, uv2, rgba4 (premultiplied; alpha 0 = additive)
static std::vector<float> partVerts;
static std::vector<uint32_t> partIdx;
static void buildParticleMesh(V3 camRight, V3 camUp) {
    partVerts.clear();
    partIdx.clear();
    for (auto& q : parts) {
        float fade = sat(q.life / q.maxLife * 3.f) * sat((q.maxLife - q.life) * 12.f + 0.3f);
        float a = q.a * fade;
        float r = q.r * a, g = q.g * a, b = q.b * a, oa = q.additive ? 0.f : a;
        V3 R = camRight * q.size, U = camUp * q.size;
        V3 c[4] = {q.p - R - U, q.p + R - U, q.p + R + U, q.p - R + U};
        float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        uint32_t base = (uint32_t)(partVerts.size() / 9);
        for (int k = 0; k < 4; k++) partVerts.insert(partVerts.end(), {c[k].x, c[k].y, c[k].z, uv[k][0], uv[k][1], r, g, b, oa});
        partIdx.insert(partIdx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
}

// ----------------------------------------------------------------------------
// Audio: every sound is synthesised. One-shots are pre-rendered at startup,
// rolling / grinding / wind / water loops and a boom-bap beat run live.
// ----------------------------------------------------------------------------
static const int AR = 44100;
static std::vector<float> sfxBuf[SFX_COUNT];
struct Voice { int id = -1; double pos = 0; float rate = 1, vol = 1; };
static Voice voices[32];
static SDL_AudioDeviceID audioDev = 0;
static bool musicOn = true, audioMuted = false;

static uint32_t aseed = 12345u;
static inline float anoise() { aseed = aseed * 1664525u + 1013904223u; return (float)((aseed >> 9) & 0x7FFFFF) / 4194304.f - 1.f; }
struct OnePole { float y = 0; float lp(float x, float a) { y += a * (x - y); return y; } };
static inline float lpA(float fc) { return 1.f - std::exp(-TAU * fc / AR); }
struct SVF {   // Chamberlin state-variable filter, returns band-pass
    float l = 0, b = 0;
    float bp(float x, float f, float q) {
        float F = 2.f * std::sin(PI * std::min(f, AR * 0.2f) / AR);
        float h = x - l - q * b;
        b += F * h;
        l += F * b;
        return b;
    }
};
static inline float mtof(float m) { return 440.f * std::pow(2.f, (m - 69.f) / 12.f); }

static void genSfx() {
    auto buf = [](int id, float secs) -> std::vector<float>& { sfxBuf[id].assign((size_t)(secs * AR), 0.f); return sfxBuf[id]; };
    {   // ollie pop: tail snap + wooden knock
        auto& b = buf(SFX_POP, 0.2f);
        for (size_t i = 0; i < b.size(); i++) {
            float t = i / (float)AR;
            b[i] = anoise() * std::exp(-t * 380.f) * 0.9f + std::sin(TAU * 820.f * t) * std::exp(-t * 55.f) * 0.45f +
                   std::sin(TAU * 1530.f * t) * std::exp(-t * 90.f) * 0.25f + std::sin(TAU * (150.f - 80 * t) * t) * std::exp(-t * 28.f) * 0.5f;
        }
    }
    for (int hard = 0; hard < 2; hard++) {   // landing: wheels slap + deck thump
        auto& b = buf(hard ? SFX_LAND_HARD : SFX_LAND, hard ? 0.45f : 0.3f);
        OnePole f;
        for (size_t i = 0; i < b.size(); i++) {
            float t = i / (float)AR;
            float clack = anoise() * (std::exp(-t * 300.f) + 0.7f * std::exp(-std::max(0.f, t - 0.022f) * 300.f) * (t > 0.022f)) * 0.7f;
            float thump = std::sin(TAU * (hard ? 70.f : 95.f) * t * (1 - t)) * std::exp(-t * (hard ? 9.f : 16.f)) * (hard ? 1.0f : 0.7f);
            float rattle = f.lp(anoise(), lpA(2500)) * std::exp(-t * 14.f) * 0.35f;
            b[i] = clack + thump + rattle;
        }
    }
    {   // metal grind start: inharmonic clank
        auto& b = buf(SFX_GRIND, 0.5f);
        float fr[5] = {1210, 1873, 2651, 3913, 5022};
        for (size_t i = 0; i < b.size(); i++) {
            float t = i / (float)AR, s = 0;
            for (int k = 0; k < 5; k++) s += std::sin(TAU * fr[k] * t) * std::exp(-t * (7.f + k * 4.f)) / (1 + k * 0.5f);
            b[i] = s * 0.35f + anoise() * std::exp(-t * 60.f) * 0.5f;
        }
    }
    {   // slide start: gritty scrape
        auto& b = buf(SFX_SLIDE, 0.35f);
        SVF f;
        for (size_t i = 0; i < b.size(); i++) {
            float t = i / (float)AR;
            b[i] = f.bp(anoise(), 900.f + 400.f * std::sin(t * 40.f), 0.4f) * std::exp(-t * 7.f) * 1.2f + anoise() * std::exp(-t * 80.f) * 0.4f;
        }
    }
    {   // bail: body slam, board clatter, a grunt
        auto& b = buf(SFX_BAIL, 0.9f);
        OnePole f;
        SVF v1, v2;
        for (size_t i = 0; i < b.size(); i++) {
            float t = i / (float)AR;
            float slam = std::sin(TAU * 60.f * t) * std::exp(-t * 7.f) * 0.9f + f.lp(anoise(), lpA(600)) * std::exp(-t * 5.f) * 1.3f;
            float clat = 0;
            for (int k = 0; k < 4; k++) { float tk = t - 0.18f - k * 0.11f; if (tk > 0) clat += anoise() * std::exp(-tk * 90.f) * (0.5f - k * 0.1f); }
            float g = 0;
            if (t < 0.25f) {   // "oof": formant-filtered pulse train
                float pitch = 150.f - 120.f * t;
                float ph = std::fmod(t * pitch, 1.f);
                float src = (ph < 0.08f ? 1.f : 0.f) - 0.08f;
                g = (v1.bp(src, 600.f, 0.3f) + v2.bp(src, 1000.f, 0.3f) * 0.6f) * std::sin(PI * t / 0.25f) * 1.5f;
            }
            b[i] = slam + clat + g;
        }
    }
    {   // splash
        auto& b = buf(SFX_SPLASH, 1.3f);
        OnePole f;
        for (size_t i = 0; i < b.size(); i++) {
            float t = i / (float)AR;
            float cut = 5000.f * std::exp(-t * 2.5f) + 300.f;
            float n = f.lp(anoise(), lpA(cut)) * std::exp(-t * 2.6f) * 1.6f;
            float bub = 0;
            for (int k = 0; k < 5; k++) { float tk = t - 0.15f - k * 0.13f; if (tk > 0) bub += std::sin(TAU * (500.f + k * 170.f) * tk * (1 + tk * 6)) * std::exp(-tk * 30.f) * 0.2f; }
            b[i] = n + bub;
        }
    }
    auto chime = [&](int id, const float* notes, int n, float gap, float dec, float amp) {
        auto& b = buf(id, gap * n + 0.9f);
        for (size_t i = 0; i < b.size(); i++) {
            float t = i / (float)AR, s = 0;
            for (int k = 0; k < n; k++) {
                float tk = t - k * gap;
                if (tk < 0) continue;
                float f0 = mtof(notes[k]);
                s += (std::sin(TAU * f0 * tk) + 0.35f * std::sin(TAU * f0 * 2.f * tk) + 0.15f * std::sin(TAU * f0 * 3.01f * tk)) * std::exp(-tk * dec);
            }
            b[i] = s * amp;
        }
    };
    { float n[3] = {72, 76, 79}; chime(SFX_COMBO, n, 3, 0.06f, 6.f, 0.28f); }
    { float n[5] = {72, 76, 79, 84, 88}; chime(SFX_BIGCOMBO, n, 5, 0.07f, 4.5f, 0.26f); }
    { float n[6] = {84, 88, 91, 96, 91, 96}; chime(SFX_LETTER, n, 6, 0.045f, 7.f, 0.2f); }
    { float n[2] = {79, 86}; chime(SFX_GAP, n, 2, 0.08f, 8.f, 0.25f); }
    { float n[1] = {84}; chime(SFX_MENU, n, 1, 0.f, 14.f, 0.25f); }
    {   // clack (sidewalk joints, curb)
        auto& b = buf(SFX_CLACK, 0.08f);
        for (size_t i = 0; i < b.size(); i++) {
            float t = i / (float)AR;
            b[i] = anoise() * std::exp(-t * 500.f) * 0.8f + std::sin(TAU * 1900.f * t) * std::exp(-t * 120.f) * 0.3f;
        }
    }
    {   // flip whoosh
        auto& b = buf(SFX_FLIP, 0.25f);
        SVF f;
        for (size_t i = 0; i < b.size(); i++) {
            float t = i / (float)AR;
            b[i] = f.bp(anoise(), 800.f + 3000.f * t, 0.5f) * std::sin(PI * t / 0.25f) * 0.6f;
        }
    }
    {   // car horn (two detuned square-ish tones)
        auto& b = buf(SFX_HONK, 0.5f);
        OnePole f;
        for (size_t i = 0; i < b.size(); i++) {
            float t = i / (float)AR;
            float s = (std::sin(TAU * 370.f * t) > 0 ? 1.f : -1.f) + (std::sin(TAU * 466.f * t) > 0 ? 1.f : -1.f);
            float env = std::min(1.f, t * 60.f) * std::min(1.f, (0.5f - t) * 25.f);
            b[i] = f.lp(s, lpA(1800)) * env * 0.35f;
        }
    }
    {   // pigeons taking off
        auto& b = buf(SFX_PIGEONS, 1.1f);
        SVF f;
        for (size_t i = 0; i < b.size(); i++) {
            float t = i / (float)AR;
            float flap = std::pow(std::max(0.f, std::sin(TAU * 17.f * t + std::sin(t * 9.f))), 3.f);
            b[i] = f.bp(anoise(), 1400.f, 0.6f) * flap * std::exp(-t * 2.2f) * 1.6f;
        }
    }
    {   // "hey!" -- formant shout
        auto& b = buf(SFX_HEY, 0.35f);
        SVF f1, f2, f3;
        for (size_t i = 0; i < b.size(); i++) {
            float t = i / (float)AR;
            float pitch = 210.f + 60.f * std::sin(PI * t / 0.35f);
            float ph = std::fmod(t * pitch, 1.f);
            float src = (ph < 0.1f ? 1.f : 0.f) - 0.1f + anoise() * 0.05f;
            float s = f1.bp(src, 560.f, 0.2f) + f2.bp(src, 1800.f, 0.25f) * 0.7f + f3.bp(src, 2600.f, 0.3f) * 0.3f;
            float env = std::min(1.f, t * 40.f) * std::exp(-std::max(0.f, t - 0.18f) * 18.f);
            b[i] = s * env * 1.4f;
        }
    }
}

static void normaliseSfx() {
    static const float target[SFX_COUNT] = {0.9f, 0.85f, 0.95f, 0.7f, 0.7f, 0.95f, 0.9f, 0.6f, 0.65f, 0.6f, 0.6f, 0.4f, 0.6f, 0.7f, 0.8f, 0.5f, 0.55f};
    for (int id = 0; id < SFX_COUNT; id++) {
        float pk = 1e-6f;
        for (float v : sfxBuf[id]) pk = std::max(pk, std::fabs(v));
        float k = target[id] / pk;
        for (float& v : sfxBuf[id]) v *= k;
        // tiny fade-out avoids clicks
        size_t n = sfxBuf[id].size(), f = std::min<size_t>(n, 200);
        for (size_t i = 0; i < f; i++) sfxBuf[id][n - 1 - i] *= (float)i / f;
    }
}

static void sfx(int id, float vol, float pitch) {
    if (!audioDev || id < 0 || id >= SFX_COUNT || sfxBuf[id].empty()) return;
    SDL_LockAudioDevice(audioDev);
    int best = 0;
    double bestPos = -1;
    for (int i = 0; i < 32; i++) {
        if (voices[i].id < 0) { best = i; bestPos = 1e18; break; }
        if (voices[i].pos > bestPos) { bestPos = voices[i].pos; best = i; }
    }
    voices[best].id = id;
    voices[best].pos = 0;
    voices[best].rate = pitch;
    voices[best].vol = vol;
    SDL_UnlockAudioDevice(audioDev);
}

// --- live synthesis state (audio thread only) ---
struct LiveState {
    float roll = 0, grind = 0, wind = 0, water = 0, metal = 0;
    OnePole rollLp1, rollLp2, ambLp, windLp, waterLp;
    SVF grindBp, grindBp2, windBp;
    double t = 0;
    // music
    long step = -1;
    float kickT = 9, snareT = 9, hatT = 9, hatDecay = 40, bassT = 9, bassF = 55, bassLen = 0.5f;
    float keyF[5] = {0}, keyT = 9, keyAmp = 0;
    int keyN = 0;
    OnePole bassLp, snareLp, crackLp;
    SVF snareBp;
    float crackle = 0;
};
static LiveState LS;
static float musicVolume = 0.3f;

static void musicStep(long st) {
    int s = (int)(st % 16), bar = (int)((st / 16) % 4);
    static const int kickA[16] = {1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0};
    static const int kickB[16] = {1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0};
    const int* k = (bar % 2) ? kickB : kickA;
    if (k[s]) LS.kickT = 0;
    if (s == 4 || s == 12) LS.snareT = 0;
    if (s % 2 == 0 || (s == 15 && bar == 3)) { LS.hatT = 0; LS.hatDecay = (s == 14) ? 9.f : 45.f; }
    static const float roots[4] = {38, 43, 36, 45};   // D G C A
    static const float chords[4][5] = {{50, 53, 57, 60, 64}, {43, 53, 59, 64, 67}, {48, 52, 59, 62, 67}, {45, 49, 55, 58, 64}};
    if (s == 0) { LS.bassF = mtof(roots[bar]); LS.bassT = 0; LS.bassLen = 0.9f; }
    if (s == 10) { LS.bassF = mtof(roots[bar] + (bar == 3 ? 12 : 7)); LS.bassT = 0; LS.bassLen = 0.35f; }
    if (s == 13 && bar % 2 == 1) { LS.bassF = mtof(roots[bar] + 12); LS.bassT = 0; LS.bassLen = 0.2f; }
    if (s == 0 || s == 6 || (s == 11 && bar != 3)) {
        for (int i = 0; i < 5; i++) LS.keyF[i] = mtof(chords[bar][i] + 12);
        LS.keyN = 5;
        LS.keyT = 0;
        LS.keyAmp = s == 0 ? 1.f : 0.55f;
    }
}

static void audioCallback(void*, Uint8* stream, int bytes) {
    float* out = (float*)stream;
    int frames = bytes / (int)(sizeof(float) * 2);
    const float dt = 1.f / AR;
    const float bpm = 88.f, stepDur = 60.f / bpm / 4.f;
    for (int i = 0; i < frames; i++) {
        float mix = 0;
        // one-shots
        for (auto& v : voices) {
            if (v.id < 0) continue;
            const auto& b = sfxBuf[v.id];
            size_t ip = (size_t)v.pos;
            if (ip + 1 >= b.size()) { v.id = -1; continue; }
            float fr = (float)(v.pos - ip);
            mix += (b[ip] * (1 - fr) + b[ip + 1] * fr) * v.vol;
            v.pos += v.rate;
        }
        // smoothed loop parameters
        LS.roll += (aud.roll - LS.roll) * 0.0008f;
        LS.grind += (aud.grind - LS.grind) * 0.004f;
        LS.wind += (aud.wind - LS.wind) * 0.0005f;
        LS.water += (aud.water - LS.water) * 0.0005f;
        LS.metal += (aud.grindMetal - LS.metal) * 0.002f;
        float n = anoise();
        if (LS.roll > 0.001f) {
            float cut = 250.f + 700.f * std::min(aud.rollPitch, 1.6f);
            float r = LS.rollLp2.lp(LS.rollLp1.lp(n, lpA(cut)), lpA(cut * 1.4f));
            float rumble = std::sin((float)(TAU * 55.0 * LS.t)) * (0.4f + 0.6f * r);
            mix += (r * 1.6f + rumble * 0.12f) * LS.roll;
        }
        if (LS.grind > 0.001f) {
            float g = LS.grindBp.bp(n, 2400.f + 300.f * std::sin((float)LS.t * 13.f), 0.12f) * LS.metal +
                      LS.grindBp2.bp(n, 850.f, 0.35f) * (1.f - LS.metal) * 1.4f;
            float ring = (std::sin((float)(TAU * 1333.0 * LS.t)) + 0.6f * std::sin((float)(TAU * 2217.0 * LS.t))) * 0.06f * LS.metal * (0.6f + 0.4f * anoise());
            mix += (g * 0.25f + ring * 0.8f) * LS.grind;
        }
        if (LS.wind > 0.001f) mix += LS.windBp.bp(n, 500.f, 0.9f) * LS.wind * 0.35f;
        if (LS.water > 0.001f) { float lo = LS.waterLp.lp(n, lpA(1800)); mix += (n - lo) * LS.water * 0.18f; }
        mix += LS.ambLp.lp(n, lpA(140)) * 0.06f;   // distant traffic rumble
        // music
        if (musicOn) {
            long st = (long)(LS.t / stepDur);
            double sw = (st % 2) ? stepDur * 0.14 : 0.0;   // swing
            if (st != LS.step && LS.t >= st * stepDur + sw) { LS.step = st; musicStep(st); }
            float m = 0;
            float kt = LS.kickT;
            if (kt < 0.5f) m += std::sin(TAU * (45.f * kt + 2.2f * (1 - std::exp(-kt * 25.f)))) * std::exp(-kt * 7.f) * 0.95f;
            float stt = LS.snareT;
            if (stt < 0.4f) m += (LS.snareBp.bp(n, 1900.f, 0.8f) * 1.2f * std::exp(-stt * 16.f) + std::sin(TAU * 190.f * stt) * std::exp(-stt * 30.f) * 0.45f) * 0.8f;
            float ht = LS.hatT;
            if (ht < 0.3f) { float hp = n - LS.snareLp.lp(n, lpA(6000)); m += hp * std::exp(-ht * LS.hatDecay) * 0.22f; }
            float bt = LS.bassT;
            if (bt < LS.bassLen + 0.2f) {
                float env = std::min(1.f, bt * 80.f) * (bt < LS.bassLen ? 1.f : std::exp(-(bt - LS.bassLen) * 20.f)) * std::exp(-bt * 1.2f);
                float s = std::sin(TAU * LS.bassF * bt) + 0.25f * std::sin(TAU * LS.bassF * 2 * bt);
                m += LS.bassLp.lp(s, lpA(400)) * env * 0.55f;
            }
            float kyt = LS.keyT;
            if (kyt < 2.5f) {
                float env = std::min(1.f, kyt * 300.f) * std::exp(-kyt * 1.6f) * LS.keyAmp;
                float I = 1.4f * std::exp(-kyt * 4.f);
                float trem = 1.f + 0.18f * std::sin(TAU * 4.6f * kyt);
                float s = 0;
                for (int k = 0; k < LS.keyN; k++) s += std::sin(TAU * LS.keyF[k] * kyt + I * std::sin(TAU * LS.keyF[k] * kyt));
                m += s * env * trem * 0.055f;
            }
            if (anoise() > 0.9993f) LS.crackle = 1.f;   // dusty vinyl
            LS.crackle *= 0.8f;
            m += LS.crackLp.lp(anoise() * LS.crackle, 0.5f) * 0.25f + n * 0.004f;
            mix += m * musicVolume;
            LS.kickT += dt; LS.snareT += dt; LS.hatT += dt; LS.bassT += dt; LS.keyT += dt;
        }
        LS.t += dt;
        float o = audioMuted ? 0.f : std::tanh(mix * 0.9f);
        out[i * 2] = o;
        out[i * 2 + 1] = o;
    }
}

static void initAudio(bool mute) {
    genSfx();
    normaliseSfx();
    if (mute) return;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = AR;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audioCallback;
    audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!audioDev) { fprintf(stderr, "Audio disabled: %s\n", SDL_GetError()); return; }
    SDL_PauseAudioDevice(audioDev, 0);
}

// ----------------------------------------------------------------------------
// HUD: pixel-font text, meters, popups, menus
// ----------------------------------------------------------------------------
struct Popup { std::string text; Col col; float scale, t, life; };
static std::vector<Popup> popups;
static void popup(const std::string& s, Col c, float scale, float life) {
    if (popups.size() > 6) popups.erase(popups.begin());
    popups.push_back({s, c, scale, 0, life});
}
static std::string fmtNum(long long v) {
    std::string s = std::to_string(v < 0 ? -v : v), o;
    int c = 0;
    for (int i = (int)s.size() - 1; i >= 0; i--) {
        o.insert(o.begin(), s[i]);
        if (++c % 3 == 0 && i > 0) o.insert(o.begin(), ',');
    }
    return v < 0 ? "-" + o : o;
}

struct Hud {
    std::vector<float> v;
    std::vector<uint32_t> idx;
    float W = 1280, H = 720, U = 1;   // U = pixel unit (scales with window height)
    void begin(float w, float h) { v.clear(); idx.clear(); W = w; H = h; U = std::max(1.f, std::floor(h / 720.f * 2.f + 0.5f) / 2.f); }
    void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Col c, float a) {
        uint32_t b = (uint32_t)(v.size() / 8);
        float r = c.r / 255.f, g = c.g / 255.f, bl = c.b / 255.f;
        float q[4][4] = {{x0, y0, u0, v0}, {x1, y0, u1, v0}, {x1, y1, u1, v1}, {x0, y1, u0, v1}};
        for (auto& p : q) v.insert(v.end(), {p[0], p[1], p[2], p[3], r, g, bl, a});
        idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
    }
    void rect(float x, float y, float w, float h, Col c, float a) { quad(x, y, x + w, y + h, -1, 0, -1, 0, c, a); }
    void frameRect(float x, float y, float w, float h, float t, Col c, float a) {
        rect(x, y, w, t, c, a); rect(x, y + h - t, w, t, c, a); rect(x, y + t, t, h - 2 * t, c, a); rect(x + w - t, y + t, t, h - 2 * t, c, a);
    }
    float textW(const std::string& s, float sc) const { return s.empty() ? 0 : (s.size() * 6 - 1) * sc; }
    // align: 0 left, 1 centre, 2 right. sc = size of one font pixel in screen pixels.
    float text(float x, float y, float sc, const std::string& s, Col c, float a, int align = 0, bool shadow = true) {
        float w = textW(s, sc);
        if (align == 1) x -= w * 0.5f;
        if (align == 2) x -= w;
        x = std::floor(x); y = std::floor(y);
        if (shadow) drawStr(x + sc * 0.6f + 0.5f, y + sc * 0.6f + 0.5f, sc, s, Col(0, 0, 0), a * 0.75f);
        drawStr(x, y, sc, s, c, a);
        return w;
    }
    void drawStr(float x, float y, float sc, const std::string& s, Col c, float a) {
        for (char ch : s) {
            int code = (unsigned char)ch;
            if (code >= 'a' && code <= 'z') code -= 32;
            if (code < 32 || code > 126) code = '?';
            if (code != ' ') {
                int i = code - 32, col = i % 16, row = i / 16;
                float u0 = col * 8 / 128.f, v0 = row * 8 / 48.f, u1 = (col * 8 + 5) / 128.f, v1 = (row * 8 + 7) / 48.f;
                quad(x, y, x + 5 * sc, y + 7 * sc, u0, v0, u1, v1, c, a);
            }
            x += 6 * sc;
        }
    }
    void vignette(float strength) {
        uint32_t b = (uint32_t)(v.size() / 8);
        float q[4][4] = {{0, 0, -2, 1}, {W, 0, -4, 1}, {W, H, -4, -1}, {0, H, -2, -1}};
        for (auto& p : q) v.insert(v.end(), {p[0], p[1], p[2], p[3], 0, 0, 0, strength});
        idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
    }
};
static Hud hud;

// 128x48 R8 atlas of the 5x7 font
static std::vector<uint8_t> buildFontAtlas() {
    std::vector<uint8_t> img(128 * 48, 0);
    for (int c = 32; c < 127; c++) {
        int i = c - 32, col = i % 16, row = i / 16;
        for (int y = 0; y < 7; y++)
            for (int x = 0; x < 5; x++)
                if (glyphPixel(c, x, y)) img[(row * 8 + y) * 128 + col * 8 + x] = 255;
    }
    return img;
}

static const char* HELP_LINES[] = {
    "W / UP ............ PUSH",
    "S / DOWN .......... BRAKE",
    "A D / LEFT RIGHT .. STEER / SPIN / BALANCE",
    "SPACE ............. OLLIE (HOLD = HIGHER)",
    "J or Z + DIR ...... FLIP TRICKS",
    "K or X + DIR ...... GRABS (HOLD, LET GO TO LAND)",
    "L or C + DIR ...... GRIND / SLIDE NEAR RAILS",
    "I or SHIFT ........ MANUAL (+W NOSE) LINKS COMBOS",
    "",
    "FLIPS: -  KICKFLIP   A HEELFLIP   D SHOVE-IT",
    "  W IMPOSSIBLE   S 360 FLIP   PRESS AGAIN = DOUBLE",
    "GRABS: - INDY  A MELON  D METHOD  W NOSE  S TAIL",
    "GRINDS: - 50-50  A BOARDSLIDE  W NOSEGRIND  S 5-0",
    "",
    "R RESET  V CAMERA  M MUSIC  T 2-MIN SESSION",
    "H HIDE HELP   ESC PAUSE   F11 FULLSCREEN",
};

static float helpPanelW(float sc) { return 52 * 6 * sc + 24 * hud.U; }
static void drawHelpPanel(float x, float y, float a, float scale = 2.f) {
    float sc = scale * hud.U;
    float lh = 10 * sc;
    int n = (int)(sizeof(HELP_LINES) / sizeof(HELP_LINES[0]));
    float w = helpPanelW(sc), h = n * lh + 40 * hud.U;
    hud.rect(x, y, w, h, Col(10, 12, 18), 0.72f * a);
    hud.frameRect(x, y, w, h, 2 * hud.U, hexc(0xffd23a), 0.8f * a);
    hud.text(x + 12 * hud.U, y + 10 * hud.U, sc, "CONTROLS", hexc(0xffd23a), a);
    for (int i = 0; i < n; i++) hud.text(x + 12 * hud.U, y + 30 * hud.U + i * lh, sc, HELP_LINES[i], Col(235, 235, 230), a);
}

// Full trick table, generated from the trick definitions
static void drawTrickPanel(float x, float y, float a) {
    float U = hud.U, sc = 1.5f * U, lh = 10 * sc;
    static const char* DIRS[9] = {"-  ", "A  ", "D  ", "W  ", "S  ", "W+A", "W+D", "S+A", "S+D"};
    float colW = 30 * 6 * sc;
    float w = colW * 3 + 24 * U, h = 16 * lh + 40 * U;
    hud.rect(x, y, w, h, Col(10, 12, 18), 0.78f * a);
    hud.frameRect(x, y, w, h, 2 * U, hexc(0xffd23a), 0.8f * a);
    const char* heads[3] = {"FLIPS  (J / Z)", "GRABS  (K / X, HOLD)", "GRINDS (L / C)"};
    for (int c = 0; c < 3; c++) {
        float cx = x + 12 * U + c * colW;
        hud.text(cx, y + 10 * U, sc, heads[c], hexc(0xffd23a), a);
        for (int i = 0; i < 9; i++) {
            const char* nm = c == 0 ? FLIPS[i].name : (c == 1 ? GRABS[i].name : GRINDS[i].name);
            int pts = c == 0 ? FLIPS[i].pts : (c == 1 ? GRABS[i].pts : GRINDS[i].pts);
            char b[64];
            snprintf(b, sizeof b, "%s %s %d", DIRS[i], nm, pts);
            hud.text(cx, y + 30 * U + i * lh, sc, b, Col(235, 235, 230), a);
        }
    }
    static const char* notes[5] = {
        "SPINS: HOLD A / D IN THE AIR.  180=150  360=400  540=750  720=1200",
        "COMBO = TRICK POINTS X NUMBER OF TRICKS.  MANUALS (I) LINK COMBOS.",
        "PRESS THE FLIP KEY AGAIN MID-FLIP FOR DOUBLES.  HOLD GRABS FOR MORE.",
        "LAND STRAIGHT, FINISH YOUR FLIPS AND LET GO OF GRABS - OR YOU BAIL!",
        "GAPS: CLEAR CABS, HYDRANTS, STAIRS, THE FOUNTAIN ... FOR BONUS POINTS",
    };
    for (int i = 0; i < 5; i++) hud.text(x + 12 * U, y + 30 * U + (10.5f + i) * lh, sc, notes[i], i == 3 ? hexc(0xff9a8a) : Col(200, 220, 255), a);
}

static bool projectToScreen(const M4& vp, V3 p, float& sx, float& sy) {
    float x = vp.m[0] * p.x + vp.m[4] * p.y + vp.m[8] * p.z + vp.m[12];
    float y = vp.m[1] * p.x + vp.m[5] * p.y + vp.m[9] * p.z + vp.m[13];
    float w = vp.m[3] * p.x + vp.m[7] * p.y + vp.m[11] * p.z + vp.m[15];
    if (w < 0.1f) return false;
    sx = (x / w * 0.5f + 0.5f) * hud.W;
    sy = (1.f - (y / w * 0.5f + 0.5f)) * hud.H;
    return true;
}

static void drawGameHud(const Player& pl, float time, float sessionLeft, bool session, int helpPage, float helpAlpha, const M4& vp, V3 cam, bool showFps, float fps) {
    bool showHelp = helpPage > 0;
    float U = hud.U;
    // score
    hud.text(24 * U, 20 * U, 2 * U, "SCORE", hexc(0xffd23a), 1);
    hud.text(24 * U, 38 * U, 5 * U, fmtNum(pl.score), Col(255, 255, 255), 1);
    hud.text(24 * U, 80 * U, 2 * U, "BEST " + fmtNum(pl.best), Col(200, 200, 200), 0.9f);
    if (session) {
        int s = (int)std::ceil(std::max(0.f, sessionLeft));
        char b[32];
        snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
        Col c = sessionLeft < 10 ? (std::fmod(time, 0.5f) < 0.25f ? hexc(0xff4a3a) : Col(255, 255, 255)) : Col(255, 255, 255);
        hud.text(hud.W * 0.5f, 20 * U, 2 * U, "SESSION", hexc(0xffd23a), 1, 1);
        hud.text(hud.W * 0.5f, 38 * U, 5 * U, b, c, 1, 1);
    }
    // S K A T E
    static const char* L[5] = {"S", "K", "A", "T", "E"};
    for (int i = 0; i < 5; i++) {
        float bx = hud.W - (5 - i) * 34 * U - 20 * U, by = 20 * U;
        bool got = pl.letters[i];
        hud.rect(bx, by, 28 * U, 30 * U, got ? hexc(0xffd23a) : Col(20, 20, 26), got ? 0.95f : 0.55f);
        hud.text(bx + 14 * U, by + 8 * U, 2 * U, L[i], got ? Col(20, 20, 20) : Col(120, 120, 130), 1, 1, false);
    }
    hud.text(hud.W - 20 * U, 58 * U, 1.5f * U, "FIND THE FLOATING LETTERS", Col(200, 200, 200), 0.7f, 2);
    // speech bubbles
    for (auto& b : bubbles) {
        float sx, sy;
        if (len(b.pos - cam) > 40.f || !projectToScreen(vp, b.pos, sx, sy)) continue;
        float a = std::min(1.f, b.t * 3.f);
        float sc = 1.5f * U;
        float w = hud.textW(b.text, sc) + 14 * U, h = 7 * sc + 12 * U;
        hud.rect(sx - w / 2, sy - h, w, h, Col(250, 250, 245), 0.92f * a);
        hud.rect(sx - 4 * U, sy, 8 * U, 6 * U, Col(250, 250, 245), 0.92f * a);
        hud.text(sx, sy - h + 6 * U, sc, b.text, Col(20, 20, 20), a, 1, false);
    }
    // combo
    float cy = hud.H - 120 * U;
    if (pl.combo.active()) {
        std::string t = pl.combo.text(200);
        float sc = 2 * U;
        // wrap into lines of ~70 chars
        std::vector<std::string> lines;
        while (t.size() > 70) {
            size_t cut = t.rfind(" + ", 70);
            if (cut == std::string::npos || cut < 20) cut = 70;
            lines.push_back(t.substr(0, cut));
            t = t.substr(cut);
        }
        lines.push_back(t);
        float y = cy - lines.size() * 10 * sc;
        for (auto& l : lines) { hud.text(hud.W * 0.5f, y, sc, l, Col(255, 255, 255), 1, 1); y += 10 * sc; }
        std::string v = fmtNum((long long)pl.combo.base) + " X " + std::to_string(pl.combo.mult);
        hud.text(hud.W * 0.5f, cy + 6 * U, 4 * U, v, hexc(0xffd23a), 1, 1);
    }
    // balance meters
    if (pl.state == ST_GRIND) {
        float w = 260 * U, x = hud.W * 0.5f - w / 2, y = cy - 70 * U;
        hud.rect(x, y, w, 12 * U, Col(15, 15, 20), 0.6f);
        hud.rect(x + w * 0.35f, y, w * 0.3f, 12 * U, hexc(0x3aa84a), 0.5f);
        float nx = x + w * 0.5f + pl.bal * w * 0.5f;
        Col nc = std::fabs(pl.bal) > 0.7f ? hexc(0xff4a3a) : Col(255, 255, 255);
        hud.rect(nx - 3 * U, y - 5 * U, 6 * U, 22 * U, nc, 1);
        hud.text(hud.W * 0.5f, y - 20 * U, 1.5f * U, "BALANCE  A / D", Col(230, 230, 230), 0.9f, 1);
    } else if (pl.state == ST_MANUAL) {
        float h = 180 * U, x = hud.W * 0.5f + 150 * U, y = hud.H * 0.5f - h / 2;
        hud.rect(x, y, 12 * U, h, Col(15, 15, 20), 0.6f);
        hud.rect(x, y + h * 0.35f, 12 * U, h * 0.3f, hexc(0x3aa84a), 0.5f);
        float ny = y + h * 0.5f + pl.bal * h * 0.5f;
        Col nc = std::fabs(pl.bal) > 0.7f ? hexc(0xff4a3a) : Col(255, 255, 255);
        hud.rect(x - 5 * U, ny - 3 * U, 22 * U, 6 * U, nc, 1);
        hud.text(x + 6 * U, y - 18 * U, 1.5f * U, "W / S", Col(230, 230, 230), 0.9f, 1);
    }
    // popups
    float py = hud.H * 0.3f;
    for (auto& p : popups) {
        float a = sat(p.t * 8.f) * sat((p.life - p.t) * 2.5f);
        float rise = p.t * 14 * U;
        float sc = std::floor(3.f * p.scale * U * 2.f) / 2.f;
        hud.text(hud.W * 0.5f, py - rise, sc, p.text, p.col, a, 1);
        py += 10 * sc;
    }
    // speed + hints
    char sb[48];
    snprintf(sb, sizeof sb, "%d MPH", (int)(len(pl.vel) * 2.237f + 0.5f));
    hud.text(24 * U, hud.H - 30 * U, 2 * U, sb, Col(220, 220, 220), 0.8f);
    if (!showHelp) hud.text(hud.W - 24 * U, hud.H - 30 * U, 1.5f * U, "H: CONTROLS / TRICK LIST   M: MUSIC   V: CAMERA   ESC: PAUSE", Col(220, 220, 220), 0.75f, 2);
    else hud.text(hud.W - 24 * U, hud.H - 30 * U, 1.5f * U, helpPage == 1 ? "H: TRICK LIST" : "H: HIDE", hexc(0xffd23a), 0.9f, 2);
    if (helpPage == 1 && helpAlpha > 0.01f) drawHelpPanel(20 * U, 110 * U, helpAlpha, 1.5f);
    if (helpPage == 2 && helpAlpha > 0.01f) drawTrickPanel(20 * U, 110 * U, helpAlpha);
    if (showFps) { snprintf(sb, sizeof sb, "%.0f FPS", fps); hud.text(24 * U, hud.H - 50 * U, 1.5f * U, sb, Col(160, 255, 160), 0.8f); }
}

static void drawTitle(float time) {
    float U = hud.U;
    hud.rect(0, 0, hud.W, hud.H, Col(0, 0, 0), 0.25f);
    float y = hud.H * 0.16f;
    float wob = std::sin(time * 2.f) * 3 * U;
    hud.text(hud.W * 0.5f + 5 * U, y + 5 * U + wob, 10 * U, "CONCRETE JUNGLE", hexc(0xc01e1e), 1, 1, false);
    hud.text(hud.W * 0.5f, y + wob, 10 * U, "CONCRETE JUNGLE", hexc(0xffd23a), 1, 1, false);
    hud.text(hud.W * 0.5f, y + 86 * U, 2.5f * U, "NEW YORK CITY STREET SKATING", Col(255, 255, 255), 1, 1);
    bool blink = std::fmod(time, 1.f) < 0.65f;
    hud.text(hud.W * 0.5f, hud.H * 0.42f, 3 * U, "PRESS ENTER TO FREE SKATE", blink ? Col(255, 255, 255) : Col(200, 200, 200), 1, 1);
    hud.text(hud.W * 0.5f, hud.H * 0.42f + 34 * U, 2.5f * U, "T - 2 MINUTE SESSION (HIGH SCORE)", Col(230, 230, 230), 1, 1);
    hud.text(hud.W * 0.5f, hud.H * 0.42f + 60 * U, 2 * U, "ESC - QUIT", Col(200, 200, 200), 0.9f, 1);
    drawHelpPanel(hud.W * 0.5f - helpPanelW(1.5f * U) / 2, hud.H * 0.55f, 1, 1.5f);
}
static void drawPause() {
    float U = hud.U;
    hud.rect(0, 0, hud.W, hud.H, Col(0, 0, 0), 0.5f);
    hud.text(hud.W * 0.5f, hud.H * 0.14f, 7 * U, "PAUSED", hexc(0xffd23a), 1, 1);
    hud.text(hud.W * 0.5f, hud.H * 0.14f + 64 * U, 2 * U, "ESC RESUME    Q QUIT TO TITLE    R RESET SKATER", Col(240, 240, 240), 1, 1);
    drawTrickPanel(hud.W * 0.5f - (90 * 6 * 1.5f * U + 24 * U) / 2, hud.H * 0.3f, 1);
}
static void drawResults(long long score, long long best, bool newBest, float time) {
    float U = hud.U;
    hud.rect(0, 0, hud.W, hud.H, Col(0, 0, 0), 0.55f);
    hud.text(hud.W * 0.5f, hud.H * 0.2f, 8 * U, "TIME'S UP!", hexc(0xffd23a), 1, 1);
    hud.text(hud.W * 0.5f, hud.H * 0.38f, 3 * U, "SESSION SCORE", Col(230, 230, 230), 1, 1);
    hud.text(hud.W * 0.5f, hud.H * 0.38f + 30 * U, 7 * U, fmtNum(score), Col(255, 255, 255), 1, 1);
    if (newBest && std::fmod(time, 0.8f) < 0.55f) hud.text(hud.W * 0.5f, hud.H * 0.38f + 96 * U, 3 * U, "NEW SESSION RECORD!", hexc(0xff9ad8), 1, 1);
    hud.text(hud.W * 0.5f, hud.H * 0.38f + 130 * U, 2.5f * U, "SESSION RECORD " + fmtNum(best), Col(220, 220, 220), 1, 1);
    hud.text(hud.W * 0.5f, hud.H * 0.75f, 2.5f * U, "ENTER - SKATE AGAIN     ESC - FREE SKATE", Col(255, 255, 255), 1, 1);
}

// ----------------------------------------------------------------------------
// Renderer
// ----------------------------------------------------------------------------
struct Lighting {
    V3 sunDir = norm(V3(-0.62f, 0.5f, 0.6f));
    V3 sunCol = V3(1.38f, 1.14f, 0.86f);
    V3 skyTop = V3(0.3f, 0.48f, 0.76f), skyHorizon = V3(0.86f, 0.79f, 0.68f);
    V3 groundCol = V3(0.36f, 0.32f, 0.28f), fogCol = V3(0.78f, 0.74f, 0.68f);
    float fogDensity = 0.0042f;
};
static Lighting LIGHT;

struct Renderer {
    GLuint pWorld = 0, pShadow = 0, pSky = 0, pWater = 0, pPart = 0, pHud = 0;
    GpuMesh staticMesh, dynMesh, waterMesh;
    GLuint shadowFbo = 0, shadowTex = 0;
    int shadowRes = 2048;
    GLuint reflFbo = 0, reflTex = 0, reflDepth = 0;
    int reflW = 0, reflH = 0;
    GLuint partVao = 0, partVbo = 0, partEbo = 0, hudVao = 0, hudVbo = 0, hudEbo = 0, fontTex = 0, emptyVao = 0;
    size_t partVCap = 0, partICap = 0, hudVCap = 0, hudICap = 0;
    GLuint mainFbo = 0, mainTex = 0, mainDepth = 0;   // offscreen target (screenshot mode only)
};
static Renderer RD;

static GLint U_(GLuint p, const char* n) { return gl.GetUniformLocation(p, n); }
static void setMat(GLuint p, const char* n, const M4& m) { gl.UniformMatrix4fv(U_(p, n), 1, GL_FALSE, m.m); }
static void set3(GLuint p, const char* n, V3 v) { gl.Uniform3f(U_(p, n), v.x, v.y, v.z); }
static void setCommon(GLuint p, V3 cam, float time) {
    gl.UseProgram(p);
    set3(p, "uSunDir", LIGHT.sunDir); set3(p, "uSunCol", LIGHT.sunCol);
    set3(p, "uSkyTop", LIGHT.skyTop); set3(p, "uSkyHorizon", LIGHT.skyHorizon);
    set3(p, "uGroundCol", LIGHT.groundCol); set3(p, "uFogCol", LIGHT.fogCol);
    set3(p, "uCamPos", cam);
    gl.Uniform1f(U_(p, "uTime"), time);
    gl.Uniform1f(U_(p, "uFogDensity"), LIGHT.fogDensity);
}

static GLuint makeTex(int w, int h, GLenum ifmt, GLenum fmt, GLenum type, GLenum filter, const void* data) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, type, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static void resizeReflection(int w, int h) {
    w = std::max(64, w / 2); h = std::max(64, h / 2);
    if (w == RD.reflW && h == RD.reflH) return;
    if (RD.reflTex) { glDeleteTextures(1, &RD.reflTex); glDeleteTextures(1, &RD.reflDepth); }
    RD.reflW = w; RD.reflH = h;
    RD.reflTex = makeTex(w, h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR, nullptr);
    RD.reflDepth = makeTex(w, h, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, GL_NEAREST, nullptr);
    if (!RD.reflFbo) gl.GenFramebuffers(1, &RD.reflFbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, RD.reflFbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, RD.reflTex, 0);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, RD.reflDepth, 0);
    if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr, "reflection FBO incomplete\n");
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void initRenderer() {
    RD.pWorld = makeProgram("world", WORLD_VS, fsWithCommon(WORLD_FS_MAIN));
    RD.pShadow = makeProgram("shadow", SHADOW_VS, SHADOW_FS);
    RD.pSky = makeProgram("sky", SKY_VS, fsWithCommon(SKY_FS_MAIN));
    RD.pWater = makeProgram("water", WATER_VS, fsWithCommon(WATER_FS_MAIN));
    RD.pPart = makeProgram("particles", PART_VS, fsWithCommon(PART_FS_MAIN));
    RD.pHud = makeProgram("hud", HUD_VS, HUD_FS);
    // shadow map
    RD.shadowTex = makeTex(RD.shadowRes, RD.shadowRes, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, GL_LINEAR, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    gl.GenFramebuffers(1, &RD.shadowFbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, RD.shadowFbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, RD.shadowTex, 0);
#ifdef __EMSCRIPTEN__
    {
        const GLenum none = GL_NONE;
        glDrawBuffers(1, &none);
    }
#else
    glDrawBuffer(GL_NONE);
#endif
    glReadBuffer(GL_NONE);
    if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr, "shadow FBO incomplete\n");
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    // font
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    std::vector<uint8_t> atlas = buildFontAtlas();
    RD.fontTex = makeTex(128, 48, GL_R8, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST, atlas.data());
    // particle + hud vertex arrays
    auto mkVao = [](GLuint& vao, GLuint& vbo, GLuint& ebo, const int* sizes, int n) {
        gl.GenVertexArrays(1, &vao); gl.GenBuffers(1, &vbo); gl.GenBuffers(1, &ebo);
        gl.BindVertexArray(vao);
        gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
        gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        int stride = 0;
        for (int i = 0; i < n; i++) stride += sizes[i];
        int off = 0;
        for (int i = 0; i < n; i++) {
            gl.EnableVertexAttribArray(i);
            gl.VertexAttribPointer(i, sizes[i], GL_FLOAT, GL_FALSE, stride * 4, (void*)(intptr_t)(off * 4));
            off += sizes[i];
        }
        gl.BindVertexArray(0);
    };
    int ps[3] = {3, 2, 4}, hs[3] = {2, 2, 4};
    mkVao(RD.partVao, RD.partVbo, RD.partEbo, ps, 3);
    mkVao(RD.hudVao, RD.hudVbo, RD.hudEbo, hs, 3);
    gl.GenVertexArrays(1, &RD.emptyVao);
}

static void streamDraw(GLuint vao, GLuint vbo, GLuint ebo, size_t& vcap, size_t& icap, const std::vector<float>& v, const std::vector<uint32_t>& idx) {
    if (idx.empty()) return;
    gl.BindVertexArray(vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
    size_t vb = v.size() * 4, ib = idx.size() * 4;
    if (vb > vcap) { vcap = vb * 2; gl.BufferData(GL_ARRAY_BUFFER, vcap, nullptr, GL_DYNAMIC_DRAW); }
    gl.BufferSubData(GL_ARRAY_BUFFER, 0, vb, v.data());
    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    if (ib > icap) { icap = ib * 2; gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, icap, nullptr, GL_DYNAMIC_DRAW); }
    gl.BufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, ib, idx.data());
    glDrawElements(GL_TRIANGLES, (GLsizei)idx.size(), GL_UNSIGNED_INT, 0);
    gl.BindVertexArray(0);
}

static M4 lightMatrix(V3 focus) {
    float R = 62.f;
    V3 eye = focus + LIGHT.sunDir * 150.f;
    M4 view = mLookAt(eye, focus, V3(0, 1, 0));
    // snap to shadow texels to avoid shimmering
    V3 ls = xPoint(view, focus);
    float texel = 2 * R / RD.shadowRes;
    V3 snapped(std::floor(ls.x / texel) * texel, std::floor(ls.y / texel) * texel, ls.z);
    view = mTranslate(V3(snapped.x - ls.x, snapped.y - ls.y, 0)) * view;
    return mOrtho(-R, R, -R, R, 1.f, 330.f) * view;
}

static void drawSceneGeometry(const M4& vp, const M4& lightVP, V3 camPos, float time, bool clip) {
    // sky
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    setCommon(RD.pSky, camPos, time);
    setMat(RD.pSky, "uInvVP", mInverse(vp));
    gl.BindVertexArray(RD.emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    // opaque world
    setCommon(RD.pWorld, camPos, time);
    setMat(RD.pWorld, "uVP", vp);
    setMat(RD.pWorld, "uLightVP", lightVP);
    if (clip) gl.Uniform4f(U_(RD.pWorld, "uClip"), 0, 1, 0, -WATER_LEVEL + 0.05f);
    else gl.Uniform4f(U_(RD.pWorld, "uClip"), 0, 0, 0, 1);
    gl.ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, RD.shadowTex);
    gl.Uniform1i(U_(RD.pWorld, "uShadow"), 0);
    gl.Uniform1f(U_(RD.pWorld, "uShadowTexel"), 1.f / RD.shadowRes);
    RD.staticMesh.draw();
    RD.dynMesh.draw();
    (void)clip;
}

// ----------------------------------------------------------------------------
// Camera
// ----------------------------------------------------------------------------
struct Camera {
    V3 pos = V3(0, 6, 40), focus = V3(0, 1, 20), look;
    float yaw = PI, fov = 64.f;
    int mode = 0;
};
static Camera cam;
static void updateCamera(float dt, const Player& pl) {
    V3 hv(pl.vel.x, 0, pl.vel.z);
    float spd = len(hv);
    float targetYaw = cam.yaw;
    if (pl.state != ST_BAIL && spd > 1.2f) targetYaw = yawOf(hv);
    float rate = pl.state == ST_AIR ? (pl.qpAir ? 0.4f : 1.6f) : (pl.state == ST_BAIL ? 0.3f : 3.6f);
    cam.yaw += wrapPi(targetYaw - cam.yaw) * (1.f - std::exp(-rate * dt));
    float dist = 4.4f + std::min(spd, 14.f) * 0.13f, height = 1.75f;
    float fov = 62.f + std::min(spd, 14.f) * 0.9f;
    if (pl.state == ST_AIR && pl.qpAir) { dist += 2.2f; height -= 0.5f; }
    if (cam.mode == 1) { dist += 4.f; height += 2.6f; }
    if (cam.mode == 2) { dist = 2.4f; height = 0.55f; fov = 96.f; }
    V3 focus = (pl.state == ST_BAIL ? pl.bodyPos : pl.pos) + V3(0, 1.05f, 0);
    V3 f = cam.focus;
    f.x = damp(f.x, focus.x, 16.f, dt);
    f.z = damp(f.z, focus.z, 16.f, dt);
    f.y = damp(f.y, focus.y, pl.state == ST_AIR ? 5.f : 10.f, dt);
    if (len(f - focus) > 8.f) f = focus;
    cam.focus = f;
    V3 desired = f + fwdYaw(cam.yaw) * -dist + V3(0, height - 0.65f, 0);
    float t = world.rayFree(f, desired);
    if (t < 1.f) desired = f + (desired - f) * std::max(0.1f, t - 0.08f);
    float gy = world.ground(desired.x, desired.z, desired.y + 0.4f).h;
    desired.y = std::max(desired.y, gy + 0.3f);
    if (len(cam.pos - desired) > 12.f) cam.pos = desired;
    cam.pos = damp3(cam.pos, desired, 10.f, dt);
    cam.look = f + fwdYaw(cam.yaw) * 1.6f + V3(0, -0.15f, 0);
    cam.fov = damp(cam.fov, fov, 3.f, dt);
}

// ----------------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------------
enum GameMode { GM_TITLE = 0, GM_PLAY, GM_PAUSE, GM_RESULTS };

struct KeySpan { int f0, f1; SDL_Scancode sc; };
static SDL_Scancode keyName(const std::string& k) {
    static const std::pair<const char*, SDL_Scancode> map[] = {
        {"W", SDL_SCANCODE_W}, {"A", SDL_SCANCODE_A}, {"S", SDL_SCANCODE_S}, {"D", SDL_SCANCODE_D}, {"SPACE", SDL_SCANCODE_SPACE},
        {"J", SDL_SCANCODE_J}, {"K", SDL_SCANCODE_K}, {"L", SDL_SCANCODE_L}, {"I", SDL_SCANCODE_I}, {"V", SDL_SCANCODE_V}};
    for (auto& m : map) if (k == m.first) return m.second;
    return SDL_SCANCODE_UNKNOWN;
}

int main(int argc, char** argv) {
    bool fullscreen = false, mute = false, hideHud = false, noHelp = false, forceTitle = false;
    int startHelpPage = -1;
    int winW = 1280, winH = 720;
    std::string shotPath;
    int shotFrames = 0;
    std::vector<KeySpan> script;
    bool startPlaying = false, startTitle = true;
    V3 startPos = SPAWN_POS;
    float startYaw = SPAWN_YAW, startSpeed = 0;
    int startCam = 0;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--fullscreen") fullscreen = true;
        else if (a == "--window") fullscreen = false;
        else if (a == "--mute") mute = true;
        else if (a == "--res") { std::string r = next(); sscanf(r.c_str(), "%dx%d", &winW, &winH); }
        else if (a == "--shot") shotPath = next();
        else if (a == "--frames") shotFrames = atoi(next().c_str());
        else if (a == "--play") { startPlaying = true; startTitle = false; }
        else if (a == "--nohud") hideHud = true;
        else if (a == "--nohelp") noHelp = true;
        else if (a == "--title") forceTitle = true;
        else if (a == "--tricks") startHelpPage = 2;
        else if (a == "--cam") startCam = atoi(next().c_str());
        else if (a == "--pos") { std::string p = next(); sscanf(p.c_str(), "%f,%f,%f", &startPos.x, &startPos.z, &startYaw); startPos.y = 5; }
        else if (a == "--speed") startSpeed = (float)atof(next().c_str());
        else if (a == "--keys") {   // e.g. "0-120:W,60-80:SPACE"
            std::string s = next();
            size_t p = 0;
            while (p < s.size()) {
                size_t c = s.find(',', p);
                std::string item = s.substr(p, c == std::string::npos ? std::string::npos : c - p);
                int f0 = 0, f1 = 0; char key[16] = {0};
                if (sscanf(item.c_str(), "%d-%d:%15s", &f0, &f1, key) == 3) script.push_back({f0, f1, keyName(key)});
                else if (sscanf(item.c_str(), "%d:%15s", &f0, key) == 2) script.push_back({f0, f0, keyName(key)});
                if (c == std::string::npos) break;
                p = c + 1;
            }
        } else if (a == "--help" || a == "-h") {
            printf("CONCRETE JUNGLE - options: --fullscreen --mute --res WxH\n");
            return 0;
        }
    }
    bool shotMode = !shotPath.empty();
    if (shotMode) mute = true;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
#ifdef __EMSCRIPTEN__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    Uint32 wflags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (shotMode) wflags |= SDL_WINDOW_HIDDEN;
    SDL_Window* win = nullptr;
    SDL_GLContext ctx = nullptr;
    for (int attempt = 0; attempt < 2 && !ctx; attempt++) {
        if (attempt == 1) {   // retry without multisampling
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        }
        win = SDL_CreateWindow("Concrete Jungle - NYC Street Skating", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH, wflags);
        if (!win) continue;
        ctx = SDL_GL_CreateContext(win);
        if (!ctx) { SDL_DestroyWindow(win); win = nullptr; }
    }
    if (!win || !ctx) { fprintf(stderr, "Could not create an OpenGL 3.3 window: %s\n", SDL_GetError()); return 1; }
    if (!gl.load()) { fprintf(stderr, "Required OpenGL functions are missing.\n"); return 1; }
    SDL_GL_SetSwapInterval(shotMode ? 0 : 1);
#ifndef __EMSCRIPTEN__
    glEnable(GL_MULTISAMPLE);
#endif

    initAudio(mute);
    initRenderer();
#ifdef __EMSCRIPTEN__
    {
        GLenum err;
        while ((err = glGetError()) != GL_NO_ERROR)
            fprintf(stderr, "WebGL error after renderer init: 0x%04x\n", (unsigned)err);
    }
#endif
    buildLevel();
    RD.staticMesh.upload(SM, false);
    RD.waterMesh.upload(WM, false);
    size_t staticTris = SM.idx.size() / 3;
    SM.clear(); SM.v.shrink_to_fit(); SM.idx.shrink_to_fit();
    initNpcs();
    initPigeons();
    initTraffic();
    P.reset(startPos, startYaw);
    P.vel = fwdYaw(startYaw) * startSpeed;
    cam.mode = startCam;
    cam.yaw = startYaw;
    cam.focus = P.pos + V3(0, 1.05f, 0);
    cam.pos = cam.focus - fwdYaw(cam.yaw) * 5.f + V3(0, 1.2f, 0);
    fprintf(stderr, "Concrete Jungle: %zu static triangles, %zu solids, %zu rails, %zu pedestrians\n", staticTris, world.solids.size(), world.rails.size(), npcs.size());

    GameMode mode = forceTitle ? GM_TITLE : (startPlaying || shotMode ? GM_PLAY : (startTitle ? GM_TITLE : GM_PLAY));
    bool running = true, showFps = false, session = false, newBest = false;
    int helpPage = startHelpPage >= 0 ? startHelpPage : (noHelp ? 0 : 1);   // 0 hidden, 1 controls, 2 trick list
    float helpTimer = 14.f, sessionLeft = 0, time = 0, fps = 60, ambientT = 8.f;
    long long sessionBest = 0, lastScoreSeen = 0;
    double acc = 0;
    Uint64 prevCounter = SDL_GetPerformanceCounter();
    Input latched;
    int frame = 0;
    Input lastIn;
    auto startSession = [&]() {
        session = true;
        sessionLeft = 120.f;
        P.score = 0;
        P.reset(SPAWN_POS, SPAWN_YAW);
        for (int i = 0; i < 5; i++) P.letters[i] = false;
        P.lettersGot = 0;
        popups.clear();
        popup("2 MINUTE SESSION - GO!", hexc(0xffd23a), 1.3f, 2.5f);
        mode = GM_PLAY;
        newBest = false;
    };

    while (running) {
        Uint64 now = SDL_GetPerformanceCounter();
        float frameDt = (float)((now - prevCounter) / (double)SDL_GetPerformanceFrequency());
        prevCounter = now;
        if (shotMode) frameDt = 1.f / 60.f;
        frameDt = std::min(frameDt, 0.1f);
        fps = damp(fps, 1.f / std::max(frameDt, 1e-4f), 2.f, frameDt);
        time += frameDt;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                SDL_Scancode sc = e.key.keysym.scancode;
                if (sc == SDL_SCANCODE_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                }
                if (mode == GM_TITLE) {
                    if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER || sc == SDL_SCANCODE_SPACE) { mode = GM_PLAY; session = false; sfx(SFX_MENU); helpTimer = 14.f; helpPage = 1; }
                    else if (sc == SDL_SCANCODE_T) { startSession(); sfx(SFX_MENU); }
                    else if (sc == SDL_SCANCODE_ESCAPE) running = false;
                } else if (mode == GM_PLAY) {
                    switch (sc) {
                        case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_P: mode = GM_PAUSE; sfx(SFX_MENU); break;
                        case SDL_SCANCODE_H: case SDL_SCANCODE_F1: helpPage = (helpPage + 1) % 3; helpTimer = 1e9f; break;
                        case SDL_SCANCODE_M: musicOn = !musicOn; popup(musicOn ? "MUSIC ON" : "MUSIC OFF", Col(220, 220, 220), 0.8f, 1.2f); break;
                        case SDL_SCANCODE_V: cam.mode = (cam.mode + 1) % 3; popup(cam.mode == 0 ? "CHASE CAM" : cam.mode == 1 ? "HIGH CAM" : "FILMER FISHEYE", Col(220, 220, 220), 0.8f, 1.2f); break;
                        case SDL_SCANCODE_R: P.reset(SPAWN_POS, SPAWN_YAW); cam.yaw = SPAWN_YAW; break;
                        case SDL_SCANCODE_T: startSession(); break;
                        case SDL_SCANCODE_F3: showFps = !showFps; break;
                        case SDL_SCANCODE_SPACE: latched.olliePress = true; break;
                        case SDL_SCANCODE_J: case SDL_SCANCODE_Z: latched.flipPress = true; break;
                        case SDL_SCANCODE_K: case SDL_SCANCODE_X: latched.grabPress = true; break;
                        case SDL_SCANCODE_L: case SDL_SCANCODE_C: latched.grindPress = true; break;
                        case SDL_SCANCODE_I: case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: latched.manualPress = true; break;
                        default: break;
                    }
                } else if (mode == GM_PAUSE) {
                    if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_P || sc == SDL_SCANCODE_RETURN) mode = GM_PLAY;
                    else if (sc == SDL_SCANCODE_Q) { mode = GM_TITLE; session = false; }
                    else if (sc == SDL_SCANCODE_R) { P.reset(SPAWN_POS, SPAWN_YAW); cam.yaw = SPAWN_YAW; mode = GM_PLAY; }
                } else if (mode == GM_RESULTS) {
                    if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) startSession();
                    else if (sc == SDL_SCANCODE_ESCAPE) { mode = GM_PLAY; session = false; }
                }
            }
        }

        // ---------------------------------------------------------------- input
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        auto held = [&](SDL_Scancode a, SDL_Scancode b = SDL_SCANCODE_UNKNOWN, SDL_Scancode c = SDL_SCANCODE_UNKNOWN) {
            bool r = ks[a] != 0;
            if (b != SDL_SCANCODE_UNKNOWN) r = r || ks[b];
            if (c != SDL_SCANCODE_UNKNOWN) r = r || ks[c];
            for (auto& s : script) if (frame >= s.f0 && frame <= s.f1 && (s.sc == a || s.sc == b || s.sc == c)) r = true;
            return r;
        };
        Input in;
        in.up = held(SDL_SCANCODE_W, SDL_SCANCODE_UP);
        in.down = held(SDL_SCANCODE_S, SDL_SCANCODE_DOWN);
        in.left = held(SDL_SCANCODE_A, SDL_SCANCODE_LEFT);
        in.right = held(SDL_SCANCODE_D, SDL_SCANCODE_RIGHT);
        in.ollie = held(SDL_SCANCODE_SPACE);
        in.flip = held(SDL_SCANCODE_J, SDL_SCANCODE_Z);
        in.grab = held(SDL_SCANCODE_K, SDL_SCANCODE_X);
        in.grind = held(SDL_SCANCODE_L, SDL_SCANCODE_C);
        in.manual = held(SDL_SCANCODE_I, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT);
        if (!script.empty()) {   // scripted presses
            in.olliePress |= in.ollie && !lastIn.ollie;
            in.flipPress |= in.flip && !lastIn.flip;
            in.grabPress |= in.grab && !lastIn.grab;
            in.grindPress |= in.grind && !lastIn.grind;
            in.manualPress |= in.manual && !lastIn.manual;
        }
        in.olliePress |= latched.olliePress; in.flipPress |= latched.flipPress; in.grabPress |= latched.grabPress;
        in.grindPress |= latched.grindPress; in.manualPress |= latched.manualPress;
        lastIn = in;
        latched = Input();

        // ---------------------------------------------------------------- simulation
        if (mode == GM_PLAY) {
            const float step = 1.f / 120.f;
            acc += frameDt;
            int n = 0;
            while (acc >= step && n < 12) {
                P.prevPos = P.pos;
                P.update(in, step);
                in.olliePress = in.flipPress = in.grabPress = in.grindPress = in.manualPress = false;
                acc -= step;
                n++;
            }
            if (n == 12) acc = 0;
            updateNpcs(frameDt, P, lastScoreSeen);
            updatePigeons(frameDt, P);
            updateTraffic(frameDt, P);
            if (session) {
                sessionLeft -= frameDt;
                if (sessionLeft <= 0 && !P.combo.active() && P.state != ST_AIR && P.state != ST_GRIND) {
                    mode = GM_RESULTS;
                    newBest = P.score > sessionBest;
                    sessionBest = std::max(sessionBest, P.score);
                    sfx(SFX_BIGCOMBO);
                }
            }
            helpTimer -= frameDt;
            if (helpTimer <= 0 && helpTimer > -1) helpPage = 0;
            ambientT -= frameDt;
            if (ambientT <= 0) { sfx(SFX_HONK, 0.07f, prng.range(0.75f, 1.0f)); ambientT = prng.range(9.f, 22.f); }
        } else if (mode == GM_TITLE) {
            updateTraffic(frameDt, P);
            updateNpcs(frameDt, P, lastScoreSeen);
        }
        for (auto& p : popups) p.t += frameDt;
        popups.erase(std::remove_if(popups.begin(), popups.end(), [](const Popup& p) { return p.t > p.life; }), popups.end());
        for (auto& b : bubbles) b.t -= frameDt;
        bubbles.erase(std::remove_if(bubbles.begin(), bubbles.end(), [](const Bubble& b) { return b.t <= 0; }), bubbles.end());

        // ---------------------------------------------------------------- camera
        if (mode == GM_TITLE) {
            cam.pos = V3(4.f * std::sin(time * 0.07f), 17.f + 2.f * std::sin(time * 0.11f), 26.f + 6.f * std::cos(time * 0.05f));
            cam.look = V3(12.f + 6.f * std::sin(time * 0.04f), 2.f, -30.f);
            cam.fov = 60;
        } else if (mode == GM_PLAY || shotMode) {
            updateCamera(frameDt, P);
        }
        {   // water ambience from nearby fountains/hydrants
            float w = 0;
            for (auto& em : emitters) if (em.kind == EM_FOUNTAIN || em.kind == EM_HYDRANT) w = std::max(w, 1.f - len(em.pos - P.pos) / 22.f);
            aud.water = mode == GM_PLAY ? std::max(0.f, w) * 0.8f : 0.f;
            if (mode != GM_PLAY) { aud.roll = aud.grind = aud.wind = 0; }
        }
        updateParticles(mode == GM_PAUSE ? 0.f : frameDt, cam.pos);

        // ---------------------------------------------------------------- render
        int W, H;
        SDL_GL_GetDrawableSize(win, &W, &H);
        resizeReflection(W, H);
        float aspect = (float)W / std::max(1, H);
        M4 proj = mPerspective(cam.fov * PI / 180.f, aspect, 0.1f, 1500.f);
        M4 view = mLookAt(cam.pos, cam.look, V3(0, 1, 0));
        M4 vp = proj * view;
        V3 camFwd = norm(cam.look - cam.pos);

        DM.clear();
        {   // draw the skater interpolated between physics ticks
            V3 simPos = P.pos;
            float alpha = mode == GM_PLAY ? (float)(acc / (1.0 / 120.0)) : 1.f;
            if (P.state != ST_BAIL && len(P.pos - P.prevPos) < 1.f) P.pos = lerp3(P.prevPos, P.pos, sat(alpha));
            drawSkater(DM, P, in);
            P.pos = simPos;
        }
        drawNpcs(DM, cam.pos);
        drawPigeons(DM, cam.pos);
        drawTraffic(DM, cam.pos);
        drawSignals(DM);
        drawLetters(DM, P, time);
        RD.dynMesh.upload(DM, true);

        V3 focus = mode == GM_TITLE ? V3(14, 0, -26) : P.pos;
        M4 lightVP = lightMatrix(focus + camFwd * 25.f);
        // shadow pass
        gl.BindFramebuffer(GL_FRAMEBUFFER, RD.shadowFbo);
        glViewport(0, 0, RD.shadowRes, RD.shadowRes);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.6f, 3.0f);
        gl.UseProgram(RD.pShadow);
        setMat(RD.pShadow, "uLightVP", lightVP);
        RD.staticMesh.draw();
        RD.dynMesh.draw();
        glDisable(GL_POLYGON_OFFSET_FILL);

        // planar reflection of the city in the river
        bool refl = cam.pos.z < 25.f || camFwd.z < -0.3f;
        if (refl) {
            gl.BindFramebuffer(GL_FRAMEBUFFER, RD.reflFbo);
            glViewport(0, 0, RD.reflW, RD.reflH);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            M4 mirror = mTranslate(V3(0, WATER_LEVEL, 0)) * mScale(V3(1, -1, 1)) * mTranslate(V3(0, -WATER_LEVEL, 0));
            M4 rvp = proj * view * mirror;
            V3 rc(cam.pos.x, 2 * WATER_LEVEL - cam.pos.y, cam.pos.z);
            drawSceneGeometry(rvp, lightVP, rc, time, true);
        }
        // main pass
        if (shotMode && !RD.mainFbo) {
            RD.mainTex = makeTex(W, H, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST, nullptr);
            RD.mainDepth = makeTex(W, H, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, GL_NEAREST, nullptr);
            gl.GenFramebuffers(1, &RD.mainFbo);
            gl.BindFramebuffer(GL_FRAMEBUFFER, RD.mainFbo);
            gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, RD.mainTex, 0);
            gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, RD.mainDepth, 0);
        }
        gl.BindFramebuffer(GL_FRAMEBUFFER, RD.mainFbo);
#ifdef __EMSCRIPTEN__
        if (!RD.mainFbo) {
            const GLenum back = GL_BACK;
            glDrawBuffers(1, &back);
        }
#endif
        glViewport(0, 0, W, H);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawSceneGeometry(vp, lightVP, cam.pos, time, false);
        // water
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        setCommon(RD.pWater, cam.pos, time);
        setMat(RD.pWater, "uVP", vp);
        setMat(RD.pWater, "uLightVP", lightVP);
        gl.Uniform2f(U_(RD.pWater, "uViewport"), (float)W, (float)H);
        gl.Uniform2f(U_(RD.pWater, "uPoolCenter"), 28.f, -34.5f);
        gl.Uniform1i(U_(RD.pWater, "uHasRefl"), refl ? 1 : 0);
        gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, RD.reflTex);
        gl.Uniform1i(U_(RD.pWater, "uReflTex"), 0);
        RD.waterMesh.draw();
        // particles
        V3 camRight = norm(cross(camFwd, V3(0, 1, 0)));
        V3 camUp = cross(camRight, camFwd);
        buildParticleMesh(camRight, camUp);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        setCommon(RD.pPart, cam.pos, time);
        setMat(RD.pPart, "uVP", vp);
        streamDraw(RD.partVao, RD.partVbo, RD.partEbo, RD.partVCap, RD.partICap, partVerts, partIdx);
        glDepthMask(GL_TRUE);
        // HUD
        glDisable(GL_DEPTH_TEST);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        hud.begin((float)W, (float)H);
        hud.vignette(0.55f);
        if (!hideHud) {
            if (mode == GM_TITLE) drawTitle(time);
            else {
                drawGameHud(P, time, sessionLeft, session, mode == GM_PLAY ? helpPage : 0, 1.f, vp, cam.pos, showFps, fps);
                if (mode == GM_PAUSE) drawPause();
                if (mode == GM_RESULTS) drawResults(P.score, sessionBest, newBest, time);
            }
        }
        gl.UseProgram(RD.pHud);
        gl.Uniform2f(U_(RD.pHud, "uScreen"), (float)W, (float)H);
        gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, RD.fontTex);
        gl.Uniform1i(U_(RD.pHud, "uFont"), 0);
        streamDraw(RD.hudVao, RD.hudVbo, RD.hudEbo, RD.hudVCap, RD.hudICap, hud.v, hud.idx);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);

        if (shotMode && frame >= shotFrames) {
            std::vector<uint8_t> px((size_t)W * H * 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            gl.BindFramebuffer(GL_FRAMEBUFFER, RD.mainFbo);
            glReadBuffer(RD.mainFbo ? GL_COLOR_ATTACHMENT0 : GL_BACK);
            glReadPixels(0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, px.data());
            FILE* f = fopen(shotPath.c_str(), "wb");
            if (f) {
                fprintf(f, "P6\n%d %d\n255\n", W, H);
                for (int y = H - 1; y >= 0; y--) fwrite(&px[(size_t)y * W * 3], 1, (size_t)W * 3, f);
                fclose(f);
            }
            static const char* SN[] = {"RIDE", "AIR", "GRIND", "MANUAL", "BAIL"};
            printf("shot frame %d pos(%.2f %.2f %.2f) state=%s score=%lld combo=%s\n", frame, P.pos.x, P.pos.y, P.pos.z, SN[P.state], P.score, P.combo.text(80).c_str());
            running = false;
        }
        SDL_GL_SwapWindow(win);
#ifdef __EMSCRIPTEN__
        emscripten_sleep(0);
#endif
        frame++;
    }
    if (audioDev) SDL_CloseAudioDevice(audioDev);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
