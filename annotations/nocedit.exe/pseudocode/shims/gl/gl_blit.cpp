// =============================================================================
// GL BLIT — the full-target textured quad, on a shader instead of glBegin
// =============================================================================
//
// See gl_blit.h for why this belongs to no renderer.

#include "shim_config.h"

#if NOCTURNE_GL_PRESENT

#include "gl/gl_blit.h"
#include "gl/gl_api.h"
#include "gl/gl_program.h"
#include "core/debug_log.h"

#include <stdint.h>
#include <stdlib.h>

namespace {

bool   g_tried    = false;   // build attempted; do not retry every frame
GLuint g_program  = 0;
GLuint g_vbo      = 0;
GLuint g_vao      = 0;
GLint  g_loc_tex  = -1;
GLint  g_attr_pos = -1;
GLint  g_attr_uv  = -1;

// #version 150 core: the earliest version with `in`/`out` and a declared
// fragment output, which is what a core profile accepts. `#version 300 es`
// under NOCTURNE_GLES (gl/gl_api.h).
//
// The quad's positions are already in clip space, so there is no projection to
// apply. A unit square under glOrtho(0, 1, 1, 0, -1, 1) maps onto NDC as
// (2x - 1, 1 - 2y) — a top-left origin, so the texture's first row lands at the
// top of the viewport — with z = 0 passing through unchanged. Those four points
// are the vertex buffer below.
const char *kVertexSource =
    NOCTURNE_GLSL_VERSION
    "in  vec2 a_pos;\n"
    "in  vec2 a_uv;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "    v_uv        = a_uv;\n"
    "}\n";

// The fragment is the texel and nothing else. Alpha comes through as it is,
// which matters for the scene-FBO seed — that target is RGBA8 and keeps what is
// written to it.
const char *kFragmentSource =
    NOCTURNE_GLSL_VERSION
    "uniform sampler2D u_tex;\n"
    "in  vec2 v_uv;\n"
    "out vec4 o_color;\n"
    "void main() {\n"
    "    o_color = texture(u_tex, v_uv);\n"
    "}\n";

// Clip-space x, y then u, v, in GL_TRIANGLE_STRIP order. Two quads: the first
// samples top-down for an uploaded CPU image, the second bottom-up for a render
// target. One buffer, two draw offsets, so the layout is stated once.
const GLfloat kQuad[32] = {
    -1.0f,  1.0f,   0.0f, 0.0f,
     1.0f,  1.0f,   1.0f, 0.0f,
    -1.0f, -1.0f,   0.0f, 1.0f,
     1.0f, -1.0f,   1.0f, 1.0f,

    -1.0f,  1.0f,   0.0f, 1.0f,
     1.0f,  1.0f,   1.0f, 1.0f,
    -1.0f, -1.0f,   0.0f, 0.0f,
     1.0f, -1.0f,   1.0f, 0.0f,
};
const int kQuadTopDown  = 0;
const int kQuadBottomUp = 4;

// Smart stretch (launcher Graphics tab, "Screen shape"). A 4:3 picture on a 16:9 screen is drawn as a
// triangle strip of narrow columns instead of one quad, with the picture's x coordinate bent so that the
// middle of the screen shows the game at close to its true proportions and the sides are stretched
// progressively more. With x in -1..1 across the screen the source position is
//     u(x) = mix(x, (4x - x^3) / 3, t)
// which reaches the picture's edge exactly at the screen's edge (u = x at t = 0, the plain quad). With
// t = 0.7 the middle is drawn about 8% wider than true and the outermost edge about 2.3 times wider.
// Linear interpolation across 48 columns follows the curve closely. Built once, appended to the same buffer after the plain quads.
const int   kWarpColumns = 48;
const float kWarpStrength = 0.7f;
const int   kWarpVerts = 2 * (kWarpColumns + 1);
const int   kWarpTopDown  = 8;
const int   kWarpBottomUp = 8 + kWarpVerts;
bool        g_warp = false;

// One draw of the picture: the plain quad, or the warped strip when smart stretch is on.
void draw_picture(int first) {
    if (g_warp) {
        gl.DrawArrays(GL_TRIANGLE_STRIP, first == kQuadBottomUp ? kWarpBottomUp : kWarpTopDown, kWarpVerts);
    } else {
        gl.DrawArrays(GL_TRIANGLE_STRIP, first, 4);
    }
}

void build_warp(GLfloat *out, bool flipped) {
    for (int i = 0; i <= kWarpColumns; ++i) {
        const float x = -1.0f + 2.0f * (float)i / (float)kWarpColumns;
        const float w = (4.0f * x - x * x * x) / 3.0f;
        const float u = 0.5f * ((1.0f - kWarpStrength) * x + kWarpStrength * w) + 0.5f;
        const float vt = flipped ? 1.0f : 0.0f, vb = flipped ? 0.0f : 1.0f;
        GLfloat *p = out + i * 8;
        p[0] = x; p[1] =  1.0f; p[2] = u; p[3] = vt;
        p[4] = x; p[5] = -1.0f; p[6] = u; p[7] = vb;
    }
}


// -----------------------------------------------------------------------------
// FSR-style upscale (an addition for the Xbox port; the shipped game has none)
// -----------------------------------------------------------------------------
//
// Two stages in one fragment shader, after AMD's FidelityFX Super Resolution 1
// (MIT licence): EASU, an edge-adaptive upscale that looks along the local edge
// direction and weights twelve source texels with a windowed-lanczos-like kernel;
// then RCAS, a contrast-adaptive sharpen. The reference runs RCAS as a second pass
// over EASU's output. Here it is folded in: the four neighbours RCAS needs are taken
// as plain bilinear samples one output pixel away, which costs 4 taps instead of
// four more EASU evaluations and a render target, and keeps this a single draw into
// the window with no extra GL state to restore.
//
// Texels are read with texelFetch so the texture's own filter does not matter for
// EASU; the RCAS neighbours use the texture's linear filter, which the caller sets.
const char *kFsrFragmentSource =
    NOCTURNE_GLSL_VERSION
    "uniform sampler2D u_tex;\n"
    "uniform vec2  u_src;\n"      // source size in texels
    "uniform float u_sharp;\n"    // RCAS strength, 0.25 (soft) .. 1.0 (strongest)
    "in  vec2 v_uv;\n"
    "out vec4 o_color;\n"
    "vec3 fetch(ivec2 p) {\n"
    "    return texelFetch(u_tex, clamp(p, ivec2(0), ivec2(u_src) - ivec2(1)), 0).rgb;\n"
    "}\n"
    "float luma(vec3 c) { return c.b * 0.5 + (c.r * 0.5 + c.g); }\n"
    "void easuSet(inout vec2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE) {\n"
    "    float lenX = max(abs(lD - lC), abs(lC - lB));\n"
    "    float dirX = lD - lB;\n"
    "    dir.x += dirX * w;\n"
    "    lenX = clamp(abs(dirX) / max(lenX, 1e-6), 0.0, 1.0);\n"
    "    lenX *= lenX;\n"
    "    len += lenX * w;\n"
    "    float lenY = max(abs(lE - lC), abs(lC - lA));\n"
    "    float dirY = lE - lA;\n"
    "    dir.y += dirY * w;\n"
    "    lenY = clamp(abs(dirY) / max(lenY, 1e-6), 0.0, 1.0);\n"
    "    lenY *= lenY;\n"
    "    len += lenY * w;\n"
    "}\n"
    "void easuTap(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len, float lob, float clp, vec3 c) {\n"
    "    vec2 v;\n"
    "    v.x = off.x * dir.x + off.y * dir.y;\n"
    "    v.y = off.x * -dir.y + off.y * dir.x;\n"
    "    v *= len;\n"
    "    float d2 = min(v.x * v.x + v.y * v.y, clp);\n"
    "    float wB = 0.4 * d2 - 1.0;\n"
    "    float wA = lob * d2 - 1.0;\n"
    "    wB *= wB;\n"
    "    wA *= wA;\n"
    "    wB = 1.5625 * wB - 0.5625;\n"
    "    float w = wB * wA;\n"
    "    aC += c * w;\n"
    "    aW += w;\n"
    "}\n"
    "vec3 easu(vec2 uv) {\n"
    "    vec2 pp = uv * u_src - 0.5;\n"
    "    vec2 fpf = floor(pp);\n"
    "    ivec2 fp = ivec2(fpf);\n"
    "    pp -= fpf;\n"
    // Source layout around the nearest texel f:      b c
    //                                              e f g h
    //                                              i j k l
    //                                                n o
    "    vec3 b = fetch(fp + ivec2( 0,-1)), c = fetch(fp + ivec2( 1,-1));\n"
    "    vec3 e = fetch(fp + ivec2(-1, 0)), f = fetch(fp);\n"
    "    vec3 g = fetch(fp + ivec2( 1, 0)), h = fetch(fp + ivec2( 2, 0));\n"
    "    vec3 i = fetch(fp + ivec2(-1, 1)), j = fetch(fp + ivec2( 0, 1));\n"
    "    vec3 k = fetch(fp + ivec2( 1, 1)), l = fetch(fp + ivec2( 2, 1));\n"
    "    vec3 n = fetch(fp + ivec2( 0, 2)), o = fetch(fp + ivec2( 1, 2));\n"
    "    float bL = luma(b), cL = luma(c), eL = luma(e), fL = luma(f), gL = luma(g), hL = luma(h);\n"
    "    float iL = luma(i), jL = luma(j), kL = luma(k), lL = luma(l), nL = luma(n), oL = luma(o);\n"
    "    vec2 dir = vec2(0.0);\n"
    "    float len = 0.0;\n"
    "    easuSet(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);\n"
    "    easuSet(dir, len,        pp.x  * (1.0 - pp.y), cL, fL, gL, hL, kL);\n"
    "    easuSet(dir, len, (1.0 - pp.x) *        pp.y,  fL, iL, jL, kL, nL);\n"
    "    easuSet(dir, len,        pp.x  *        pp.y,  gL, jL, kL, lL, oL);\n"
    "    vec2 dir2 = dir * dir;\n"
    "    float dirR = dir2.x + dir2.y;\n"
    "    bool zro = dirR < (1.0 / 32768.0);\n"
    "    dirR = inversesqrt(max(dirR, 1e-9));\n"
    "    dirR = zro ? 1.0 : dirR;\n"
    "    dir.x = zro ? 1.0 : dir.x;\n"
    "    dir *= dirR;\n"
    "    len *= 0.5;\n"
    "    len *= len;\n"
    "    float stretch = (dir.x * dir.x + dir.y * dir.y) / max(abs(dir.x), abs(dir.y));\n"
    "    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);\n"
    "    float lob = 0.5 + ((0.25 - 0.04) - 0.5) * len;\n"
    "    float clp = 1.0 / lob;\n"
    "    vec3 min4 = min(min(f, g), min(j, k));\n"
    "    vec3 max4 = max(max(f, g), max(j, k));\n"
    "    vec3 aC = vec3(0.0);\n"
    "    float aW = 0.0;\n"
    "    easuTap(aC, aW, vec2( 0.0,-1.0) - pp, dir, len2, lob, clp, b);\n"
    "    easuTap(aC, aW, vec2( 1.0,-1.0) - pp, dir, len2, lob, clp, c);\n"
    "    easuTap(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, i);\n"
    "    easuTap(aC, aW, vec2( 0.0, 1.0) - pp, dir, len2, lob, clp, j);\n"
    "    easuTap(aC, aW, vec2( 0.0, 0.0) - pp, dir, len2, lob, clp, f);\n"
    "    easuTap(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, e);\n"
    "    easuTap(aC, aW, vec2( 1.0, 1.0) - pp, dir, len2, lob, clp, k);\n"
    "    easuTap(aC, aW, vec2( 2.0, 1.0) - pp, dir, len2, lob, clp, l);\n"
    "    easuTap(aC, aW, vec2( 2.0, 0.0) - pp, dir, len2, lob, clp, h);\n"
    "    easuTap(aC, aW, vec2( 1.0, 0.0) - pp, dir, len2, lob, clp, g);\n"
    "    easuTap(aC, aW, vec2( 1.0, 2.0) - pp, dir, len2, lob, clp, o);\n"
    "    easuTap(aC, aW, vec2( 0.0, 2.0) - pp, dir, len2, lob, clp, n);\n"
    "    return min(max4, max(min4, aC / aW));\n"
    "}\n"
    "void main() {\n"
    "    vec3 e = easu(v_uv);\n"
    "    vec2 px = vec2(abs(dFdx(v_uv.x)), abs(dFdy(v_uv.y)));\n"   // one output pixel, in uv
    "    vec3 b = texture(u_tex, v_uv + vec2(0.0, -px.y)).rgb;\n"
    "    vec3 d = texture(u_tex, v_uv + vec2(-px.x, 0.0)).rgb;\n"
    "    vec3 f = texture(u_tex, v_uv + vec2( px.x, 0.0)).rgb;\n"
    "    vec3 h = texture(u_tex, v_uv + vec2(0.0,  px.y)).rgb;\n"
    "    vec3 mn4 = min(min(b, d), min(f, h));\n"
    "    vec3 mx4 = max(max(b, d), max(f, h));\n"
    "    vec3 hitMin = mn4 / (4.0 * max(mx4, vec3(1e-4)));\n"
    "    vec3 hitMax = (vec3(1.0) - mx4) / min(4.0 * mn4 - vec3(4.0), vec3(-1e-4));\n"
    "    vec3 lobeRGB = max(-hitMin, hitMax);\n"
    "    float lobe = max(-0.1875, min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * u_sharp;\n"
    "    vec3 outc = (lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0);\n"
    "    o_color = vec4(clamp(outc, 0.0, 1.0), 1.0);\n"
    "}\n";

bool   g_fsr_tried   = false;
GLuint g_fsr_program = 0;
GLint  g_fsr_loc_tex = -1;
GLint  g_fsr_loc_src = -1;
GLint  g_fsr_loc_sharp = -1;

bool build_fsr_program() {
    if (gl.Uniform1f == nullptr || gl.Uniform2f == nullptr) {
        DLOG("render","gl_blit: no Uniform1f/2f — FSR unavailable");
        return false;
    }
    GLuint program = nocturne_gl_build_program(kVertexSource, kFsrFragmentSource, "gl_blit_fsr");
    if (program == 0) {
        return false;
    }
    // The vertex array recorded by the plain blit program reads a_uv from the location that
    // program was given; this one has the same vertex shader, so it should match. If it ever
    // does not, drawing with this program would sample garbage coordinates: refuse instead.
    if (gl.GetAttribLocation(program, "a_uv") != g_attr_uv) {
        DLOG("render","gl_blit: FSR program put a_uv elsewhere — refusing");
        if (gl.DeleteProgram != nullptr) gl.DeleteProgram(program);
        return false;
    }
    g_fsr_loc_tex   = gl.GetUniformLocation(program, "u_tex");
    g_fsr_loc_src   = gl.GetUniformLocation(program, "u_src");
    g_fsr_loc_sharp = gl.GetUniformLocation(program, "u_sharp");
    g_fsr_program = program;
    DLOG("render","gl_blit: FSR program linked (id=%u)", (unsigned)program);
    return true;
}

bool have_entry_points() {
    return gl.CreateShader != nullptr && gl.ShaderSource != nullptr &&
           gl.CompileShader != nullptr && gl.GetShaderiv != nullptr &&
           gl.DeleteShader != nullptr && gl.CreateProgram != nullptr &&
           gl.AttachShader != nullptr && gl.LinkProgram != nullptr &&
           gl.GetProgramiv != nullptr && gl.UseProgram != nullptr &&
           gl.GetUniformLocation != nullptr && gl.Uniform1i != nullptr &&
           gl.GetIntegerv != nullptr &&
           gl.GenBuffers != nullptr && gl.BindBuffer != nullptr &&
           gl.BufferData != nullptr && gl.GetAttribLocation != nullptr &&
           gl.BindAttribLocation != nullptr &&
           gl.EnableVertexAttribArray != nullptr &&
           gl.VertexAttribPointer != nullptr && gl.DrawArrays != nullptr &&
           gl.GenVertexArrays != nullptr && gl.BindVertexArray != nullptr;
}

bool build_program() {
    GLuint program = nocturne_gl_build_program(kVertexSource, kFragmentSource, "gl_blit");
    if (program == 0) {
        return false;
    }

    g_loc_tex  = gl.GetUniformLocation(program, "u_tex");
    g_attr_pos = 0;                           // pinned by nocturne_gl_build_program
    g_attr_uv  = gl.GetAttribLocation(program, "a_uv");

    gl.GenBuffers(1, &g_vbo);
    gl.GenVertexArrays(1, &g_vao);
    if (g_vbo == 0 || g_vao == 0) {
        DLOG("render","gl_blit: buffer=%u array=%u — quad unavailable",
                  (unsigned)g_vbo, (unsigned)g_vao);
        if (gl.DeleteProgram != nullptr) gl.DeleteProgram(program);
        return false;
    }

    // Static: the quad is the same four vertices for the life of the context,
    // and the vertex array records how to read them, so a blit is a bind and a
    // draw. The caller's viewport decides where it lands, not the geometry.
    GLint prev_array  = 0;
    GLint prev_buffer = 0;
    gl.GetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_array);
    gl.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_buffer);

    gl.BindVertexArray(g_vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, g_vbo);
    GLfloat all[32 + 2 * kWarpVerts * 4];
    for (int i = 0; i < 32; ++i) all[i] = kQuad[i];
    build_warp(all + 32, false);
    build_warp(all + 32 + kWarpVerts * 4, true);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(all), all, GL_STATIC_DRAW);

    const GLsizei stride = (GLsizei)(4 * sizeof(GLfloat));
    gl.EnableVertexAttribArray((GLuint)g_attr_pos);
    gl.VertexAttribPointer((GLuint)g_attr_pos, 2, GL_FLOAT, GL_FALSE, stride,
                           (const void *)(intptr_t)0);
    if (g_attr_uv >= 0) {
        gl.EnableVertexAttribArray((GLuint)g_attr_uv);
        gl.VertexAttribPointer((GLuint)g_attr_uv, 2, GL_FLOAT, GL_FALSE, stride,
                               (const void *)(intptr_t)(2 * sizeof(GLfloat)));
    }

    gl.BindBuffer(GL_ARRAY_BUFFER, (GLuint)prev_buffer);
    gl.BindVertexArray((GLuint)prev_array);

    g_program = program;
    DLOG("render","gl_blit: program linked (id=%u) attrs pos=%d uv=%d",
              (unsigned)program, (int)g_attr_pos, (int)g_attr_uv);
    return true;
}

int blit_quad(unsigned int texture, int first) {
    if (g_program == 0) {
        if (g_tried) return 0;
        g_tried = true;
        if (!have_entry_points()) {
            DLOG("render","gl_blit: no shader/buffer entry points — quad unavailable");
            return 0;
        }
        if (!build_program()) return 0;
    }

    // Put back whatever the caller had. The scene-FBO seed runs mid-frame
    // between the renderer's own draws, so anything left changed here lands
    // directly on those draws.
    GLint prev_program = 0;
    GLint prev_array   = 0;
    gl.GetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
    gl.GetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_array);

    gl.UseProgram(g_program);
    // After glUseProgram, never before: glUniform writes to the currently bound
    // program, and setting it earlier leaves the sampler at its default with no
    // error to show for it. The unit is 0, which is the one the caller bound the
    // texture on — nothing here calls glActiveTexture, and the fallback quad
    // assumes the same unit.
    if (g_loc_tex >= 0) gl.Uniform1i(g_loc_tex, 0);
    gl.BindTexture(GL_TEXTURE_2D, (GLuint)texture);

    gl.BindVertexArray(g_vao);
    draw_picture(first);

    gl.BindVertexArray((GLuint)prev_array);
    gl.UseProgram((GLuint)prev_program);
    return 1;
}

int blit_quad_fsr(unsigned int texture, int first, int src_w, int src_h, float sharp) {
    // The plain program owns the vertex buffer and array, so it has to exist first.
    if (g_program == 0) {
        if (g_tried) return 0;
        g_tried = true;
        if (!have_entry_points() || !build_program()) return 0;
    }
    if (g_fsr_program == 0) {
        if (g_fsr_tried) return 0;
        g_fsr_tried = true;
        if (!build_fsr_program()) return 0;
    }

    GLint prev_program = 0;
    GLint prev_array   = 0;
    gl.GetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
    gl.GetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_array);

    gl.UseProgram(g_fsr_program);
    if (g_fsr_loc_tex >= 0) gl.Uniform1i(g_fsr_loc_tex, 0);
    if (g_fsr_loc_src >= 0) gl.Uniform2f(g_fsr_loc_src, (GLfloat)src_w, (GLfloat)src_h);
    if (g_fsr_loc_sharp >= 0) gl.Uniform1f(g_fsr_loc_sharp, sharp);
    gl.BindTexture(GL_TEXTURE_2D, (GLuint)texture);

    gl.BindVertexArray(g_vao);
    draw_picture(first);

    gl.BindVertexArray((GLuint)prev_array);
    gl.UseProgram((GLuint)prev_program);
    return 1;
}

}  // namespace

extern "C" int nocturne_gl_blit_quad(unsigned int texture) {
    return blit_quad(texture, kQuadTopDown);
}

extern "C" int nocturne_gl_blit_quad_flipped(unsigned int texture) {
    return blit_quad(texture, kQuadBottomUp);
}

extern "C" int nocturne_gl_blit_quad_fsr(unsigned int texture, int flipped, int src_w, int src_h,
                                         float sharpness) {
    if (src_w < 2 || src_h < 2) return 0;
    const float s = sharpness < 0.0f ? 0.0f : (sharpness > 1.0f ? 1.0f : sharpness);
    // 0..1 from the launcher maps to RCAS stops 2..0: exp2(-2) = 0.25 up to exp2(0) = 1.0.
    const float contrast = 0.25f + 0.75f * s * s;
    return blit_quad_fsr(texture, flipped ? kQuadBottomUp : kQuadTopDown, src_w, src_h, contrast);
}

extern "C" void nocturne_gl_blit_set_warp(int on) { g_warp = on != 0; }

extern "C" void nocturne_gl_blit_shutdown(void) {
    if (g_fsr_program != 0 && gl.DeleteProgram != nullptr) gl.DeleteProgram(g_fsr_program);
    g_fsr_program = 0;
    g_fsr_tried   = false;
    if (g_program != 0 && gl.DeleteProgram != nullptr) {
        gl.DeleteProgram(g_program);
    }
    if (g_vbo != 0 && gl.DeleteBuffers != nullptr) {
        gl.DeleteBuffers(1, &g_vbo);
    }
    if (g_vao != 0 && gl.DeleteVertexArrays != nullptr) {
        gl.DeleteVertexArrays(1, &g_vao);
    }
    g_program = 0;
    g_vbo     = 0;
    g_vao     = 0;
    g_tried   = false;
}

#else  // !NOCTURNE_GL_PRESENT

#include "gl/gl_blit.h"

extern "C" int nocturne_gl_blit_quad(unsigned int texture) { (void)texture; return 0; }
extern "C" int nocturne_gl_blit_quad_flipped(unsigned int t) { (void)t; return 0; }
extern "C" void nocturne_gl_blit_set_warp(int on) { (void)on; }
extern "C" int nocturne_gl_blit_quad_fsr(unsigned int t, int f, int w, int h, float s) { (void)t; (void)f; (void)w; (void)h; (void)s; return 0; }
extern "C" void nocturne_gl_blit_shutdown(void) {}

#endif  // NOCTURNE_GL_PRESENT
