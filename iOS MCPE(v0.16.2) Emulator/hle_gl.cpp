// OpenGL ES 2.0 (+ a few ES3 / extension entry points) on top of desktop
// OpenGL (compatibility profile), plus the host services used by the guest
// UIKit/EAGL implementation (window, events, presentation).
#include "hle.h"
#include "controls.h"
#include "window.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>

#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

namespace {
struct GlLoad {
    const char* name;
    void** ptr;
};
std::vector<GlLoad>& gl_loads() {
    static std::vector<GlLoad> v;
    return v;
}
struct GlReg {
    GlReg(const char* n, void** p) { gl_loads().push_back({n, p}); }
};
bool g_gl_loaded = false;
void load_gl() {
    if (g_gl_loaded) return;
    g_gl_loaded = true;
    int missing = 0;
    for (auto& l : gl_loads()) {
        *l.ptr = window::gl_proc(l.name);
        if (!*l.ptr) { LOGW("GL function %s not available", l.name); missing++; }
    }
    LOGI("GL: loaded %zu entry points (%d missing)", gl_loads().size(), missing);
}
bool g_gl_trace = false;
struct GlTraceEnt { const char* name; u64 a[4]; };
GlTraceEnt g_ring[64];
std::atomic<u32> g_ring_pos{0};
inline void gl_trace(const char* n) {
    if (!g_cpu) return;
    u32 i = g_ring_pos++ & 63;
    g_ring[i].name = n;
    for (int k = 0; k < 4; k++) g_ring[i].a[k] = g_cpu->x(k);
}
}  // namespace
void gl_dump_trace() {
    u32 e = g_ring_pos.load();
    for (u32 k = e > 64 ? e - 64 : 0; k < e; k++) {
        auto& t = g_ring[k & 63];
        if (t.name) LOGW("  gl[%u] %s(%llx, %llx, %llx, %llx)", k, t.name, (unsigned long long)t.a[0], (unsigned long long)t.a[1], (unsigned long long)t.a[2], (unsigned long long)t.a[3]);
    }
}

// Declares a host GL pointer + a guest-visible HLE function forwarding to it.
#define GLFN(RET, NAME, PARAMS, ARGS)                                         \
    using PFN_##NAME = RET(APIENTRY*) PARAMS;                                 \
    static PFN_##NAME p_##NAME;                                               \
    static GlReg glreg_##NAME(#NAME, reinterpret_cast<void**>(&p_##NAME));    \
    HLE_FUNC(NAME, RET, PARAMS) {                                             \
        gl_trace(#NAME);                                                      \
        if (!p_##NAME) return static_cast<RET>(0);                            \
        return p_##NAME ARGS;                                                 \
    }
// Same, but without an HLE wrapper (implemented manually below)
#define GLPTR(RET, NAME, PARAMS)              \
    using PFN_##NAME = RET(APIENTRY*) PARAMS; \
    static PFN_##NAME p_##NAME;               \
    static GlReg glreg_##NAME(#NAME, reinterpret_cast<void**>(&p_##NAME));

GLFN(void, glActiveTexture, (GLenum a), (a))
GLFN(void, glAttachShader, (GLuint a, GLuint b), (a, b))
GLFN(void, glBindAttribLocation, (GLuint a, GLuint b, const GLchar* c), (a, b, c))
GLFN(void, glBindBuffer, (GLenum a, GLuint b), (a, b))
GLFN(void, glBindFramebuffer, (GLenum a, GLuint b), (a, b))
GLFN(void, glBindRenderbuffer, (GLenum a, GLuint b), (a, b))
GLFN(void, glBindTexture, (GLenum a, GLuint b), (a, b))
GLFN(void, glBindVertexArray, (GLuint a), (a))
GLFN(void, glBlendColor, (float a, float b, float c, float d), (a, b, c, d))
GLFN(void, glBlendEquation, (GLenum a), (a))
GLFN(void, glBlendEquationSeparate, (GLenum a, GLenum b), (a, b))
GLFN(void, glBlendFunc, (GLenum a, GLenum b), (a, b))
GLFN(void, glBlendFuncSeparate, (GLenum a, GLenum b, GLenum c, GLenum d), (a, b, c, d))
GLFN(void, glBlitFramebuffer, (GLint a, GLint b, GLint c, GLint d, GLint e, GLint f, GLint g, GLint h, GLbitfield i, GLenum j), (a, b, c, d, e, f, g, h, i, j))
GLFN(void, glBufferData, (GLenum a, GLsizeiptr b, const void* c, GLenum d), (a, b, c, d))
GLFN(void, glBufferSubData, (GLenum a, GLintptr b, GLsizeiptr c, const void* d), (a, b, c, d))
GLFN(GLenum, glCheckFramebufferStatus, (GLenum a), (a))
GLFN(void, glClear, (GLbitfield a), (a))
GLFN(void, glClearColor, (float a, float b, float c, float d), (a, b, c, d))
GLFN(void, glClearDepthf, (float a), (a))
GLFN(void, glClearStencil, (GLint a), (a))
GLFN(void, glColorMask, (GLboolean a, GLboolean b, GLboolean c, GLboolean d), (a, b, c, d))
GLFN(void, glCompressedTexImage2D, (GLenum a, GLint b, GLenum c, GLsizei d, GLsizei e, GLint f, GLsizei g, const void* h), (a, b, c, d, e, f, g, h))
GLFN(void, glCompressedTexSubImage2D, (GLenum a, GLint b, GLint c, GLint d, GLsizei e, GLsizei f, GLenum g, GLsizei h, const void* i), (a, b, c, d, e, f, g, h, i))
GLFN(void, glCopyTexImage2D, (GLenum a, GLint b, GLenum c, GLint d, GLint e, GLsizei f, GLsizei g, GLint h), (a, b, c, d, e, f, g, h))
GLFN(void, glCopyTexSubImage2D, (GLenum a, GLint b, GLint c, GLint d, GLint e, GLint f, GLsizei g, GLsizei h), (a, b, c, d, e, f, g, h))
GLFN(GLuint, glCreateProgram, (), ())
GLFN(GLuint, glCreateShader, (GLenum a), (a))
GLFN(void, glCullFace, (GLenum a), (a))
GLFN(void, glDeleteBuffers, (GLsizei a, const GLuint* b), (a, b))
GLFN(void, glDeleteFramebuffers, (GLsizei a, const GLuint* b), (a, b))
GLFN(void, glDeleteProgram, (GLuint a), (a))
GLFN(void, glDeleteRenderbuffers, (GLsizei a, const GLuint* b), (a, b))
GLFN(void, glDeleteShader, (GLuint a), (a))
GLFN(void, glDeleteTextures, (GLsizei a, const GLuint* b), (a, b))
GLFN(void, glDeleteVertexArrays, (GLsizei a, const GLuint* b), (a, b))
GLFN(void, glDepthFunc, (GLenum a), (a))
GLFN(void, glDepthMask, (GLboolean a), (a))
GLFN(void, glDepthRangef, (float a, float b), (a, b))
GLFN(void, glDetachShader, (GLuint a, GLuint b), (a, b))
GLFN(void, glDisable, (GLenum a), (a))
GLFN(void, glDisableVertexAttribArray, (GLuint a), (a))
GLFN(void, glDrawArrays, (GLenum a, GLint b, GLsizei c), (a, b, c))
GLFN(void, glDrawArraysInstanced, (GLenum a, GLint b, GLsizei c, GLsizei d), (a, b, c, d))
GLFN(void, glDrawBuffers, (GLsizei a, const GLenum* b), (a, b))
GLFN(void, glDrawElements, (GLenum a, GLsizei b, GLenum c, const void* d), (a, b, c, d))
GLFN(void, glDrawElementsInstanced, (GLenum a, GLsizei b, GLenum c, const void* d, GLsizei e), (a, b, c, d, e))
GLFN(void, glEnable, (GLenum a), (a))
GLFN(void, glEnableVertexAttribArray, (GLuint a), (a))
GLFN(void, glFinish, (), ())
GLFN(void, glFlush, (), ())
GLFN(void, glFramebufferRenderbuffer, (GLenum a, GLenum b, GLenum c, GLuint d), (a, b, c, d))
GLFN(void, glFramebufferTexture2D, (GLenum a, GLenum b, GLenum c, GLuint d, GLint e), (a, b, c, d, e))
GLFN(void, glFrontFace, (GLenum a), (a))
GLFN(void, glGenBuffers, (GLsizei a, GLuint* b), (a, b))
GLFN(void, glGenerateMipmap, (GLenum a), (a))
GLFN(void, glGenFramebuffers, (GLsizei a, GLuint* b), (a, b))
GLFN(void, glGenRenderbuffers, (GLsizei a, GLuint* b), (a, b))
GLFN(void, glGenTextures, (GLsizei a, GLuint* b), (a, b))
GLFN(void, glGenVertexArrays, (GLsizei a, GLuint* b), (a, b))
GLFN(void, glGetActiveAttrib, (GLuint a, GLuint b, GLsizei c, GLsizei* d, GLint* e, GLenum* f, GLchar* g), (a, b, c, d, e, f, g))
GLFN(void, glGetActiveUniform, (GLuint a, GLuint b, GLsizei c, GLsizei* d, GLint* e, GLenum* f, GLchar* g), (a, b, c, d, e, f, g))
GLFN(void, glGetAttachedShaders, (GLuint a, GLsizei b, GLsizei* c, GLuint* d), (a, b, c, d))
GLFN(GLint, glGetAttribLocation, (GLuint a, const GLchar* b), (a, b))
GLFN(void, glGetBooleanv, (GLenum a, GLboolean* b), (a, b))
GLFN(void, glGetBufferParameteriv, (GLenum a, GLenum b, GLint* c), (a, b, c))
GLFN(GLenum, glGetError, (), ())
GLFN(void, glGetFloatv, (GLenum a, float* b), (a, b))
GLFN(void, glGetFramebufferAttachmentParameteriv, (GLenum a, GLenum b, GLenum c, GLint* d), (a, b, c, d))
GLFN(void, glGetProgramInfoLog, (GLuint a, GLsizei b, GLsizei* c, GLchar* d), (a, b, c, d))
GLFN(void, glGetProgramiv, (GLuint a, GLenum b, GLint* c), (a, b, c))
GLFN(void, glGetRenderbufferParameteriv, (GLenum a, GLenum b, GLint* c), (a, b, c))
GLFN(void, glGetShaderInfoLog, (GLuint a, GLsizei b, GLsizei* c, GLchar* d), (a, b, c, d))
GLFN(void, glGetShaderiv, (GLuint a, GLenum b, GLint* c), (a, b, c))
GLFN(void, glGetShaderSource, (GLuint a, GLsizei b, GLsizei* c, GLchar* d), (a, b, c, d))
GLFN(void, glGetTexParameteriv, (GLenum a, GLenum b, GLint* c), (a, b, c))
GLFN(void, glGetTexParameterfv, (GLenum a, GLenum b, float* c), (a, b, c))
GLFN(void, glGetUniformfv, (GLuint a, GLint b, float* c), (a, b, c))
GLFN(void, glGetUniformiv, (GLuint a, GLint b, GLint* c), (a, b, c))
GLFN(GLint, glGetUniformLocation, (GLuint a, const GLchar* b), (a, b))
GLFN(void, glGetVertexAttribfv, (GLuint a, GLenum b, float* c), (a, b, c))
GLFN(void, glGetVertexAttribiv, (GLuint a, GLenum b, GLint* c), (a, b, c))
GLFN(void, glHint, (GLenum a, GLenum b), (a, b))
GLFN(GLboolean, glIsBuffer, (GLuint a), (a))
GLFN(GLboolean, glIsEnabled, (GLenum a), (a))
GLFN(GLboolean, glIsFramebuffer, (GLuint a), (a))
GLFN(GLboolean, glIsProgram, (GLuint a), (a))
GLFN(GLboolean, glIsRenderbuffer, (GLuint a), (a))
GLFN(GLboolean, glIsShader, (GLuint a), (a))
GLFN(GLboolean, glIsTexture, (GLuint a), (a))
GLFN(GLboolean, glIsVertexArray, (GLuint a), (a))
GLFN(void, glLineWidth, (float a), (a))
GLFN(void, glLinkProgram, (GLuint a), (a))
GLFN(void, glPixelStorei, (GLenum a, GLint b), (a, b))
GLFN(void, glPolygonOffset, (float a, float b), (a, b))
GLFN(void, glReadBuffer, (GLenum a), (a))
GLFN(void, glReadPixels, (GLint a, GLint b, GLsizei c, GLsizei d, GLenum e, GLenum f, void* g), (a, b, c, d, e, f, g))
GLFN(void, glReleaseShaderCompiler, (), ())
GLFN(void, glRenderbufferStorage, (GLenum a, GLenum b, GLsizei c, GLsizei d), (a, b, c, d))
GLFN(void, glRenderbufferStorageMultisample, (GLenum a, GLsizei b, GLenum c, GLsizei d, GLsizei e), (a, b, c, d, e))
GLFN(void, glSampleCoverage, (float a, GLboolean b), (a, b))
GLFN(void, glScissor, (GLint a, GLint b, GLsizei c, GLsizei d), (a, b, c, d))
GLFN(void, glStencilFunc, (GLenum a, GLint b, GLuint c), (a, b, c))
GLFN(void, glStencilFuncSeparate, (GLenum a, GLenum b, GLint c, GLuint d), (a, b, c, d))
GLFN(void, glStencilMask, (GLuint a), (a))
GLFN(void, glStencilMaskSeparate, (GLenum a, GLuint b), (a, b))
GLFN(void, glStencilOp, (GLenum a, GLenum b, GLenum c), (a, b, c))
GLFN(void, glStencilOpSeparate, (GLenum a, GLenum b, GLenum c, GLenum d), (a, b, c, d))
GLFN(void, glTexParameterf, (GLenum a, GLenum b, float c), (a, b, c))
GLFN(void, glTexParameterfv, (GLenum a, GLenum b, const float* c), (a, b, c))
GLFN(void, glTexParameteri, (GLenum a, GLenum b, GLint c), (a, b, c))
GLFN(void, glTexParameteriv, (GLenum a, GLenum b, const GLint* c), (a, b, c))
GLFN(void, glTexStorage2D, (GLenum a, GLsizei b, GLenum c, GLsizei d, GLsizei e), (a, b, c, d, e))
GLFN(void, glUniform1f, (GLint a, float b), (a, b))
GLFN(void, glUniform1fv, (GLint a, GLsizei b, const float* c), (a, b, c))
GLFN(void, glUniform1i, (GLint a, GLint b), (a, b))
GLFN(void, glUniform1iv, (GLint a, GLsizei b, const GLint* c), (a, b, c))
GLFN(void, glUniform2f, (GLint a, float b, float c), (a, b, c))
GLFN(void, glUniform2fv, (GLint a, GLsizei b, const float* c), (a, b, c))
GLFN(void, glUniform2i, (GLint a, GLint b, GLint c), (a, b, c))
GLFN(void, glUniform2iv, (GLint a, GLsizei b, const GLint* c), (a, b, c))
GLFN(void, glUniform3f, (GLint a, float b, float c, float d), (a, b, c, d))
GLFN(void, glUniform3fv, (GLint a, GLsizei b, const float* c), (a, b, c))
GLFN(void, glUniform3i, (GLint a, GLint b, GLint c, GLint d), (a, b, c, d))
GLFN(void, glUniform3iv, (GLint a, GLsizei b, const GLint* c), (a, b, c))
GLFN(void, glUniform4f, (GLint a, float b, float c, float d, float e), (a, b, c, d, e))
GLFN(void, glUniform4fv, (GLint a, GLsizei b, const float* c), (a, b, c))
GLFN(void, glUniform4i, (GLint a, GLint b, GLint c, GLint d, GLint e), (a, b, c, d, e))
GLFN(void, glUniform4iv, (GLint a, GLsizei b, const GLint* c), (a, b, c))
GLFN(void, glUniformMatrix2fv, (GLint a, GLsizei b, GLboolean c, const float* d), (a, b, c, d))
GLFN(void, glUniformMatrix3fv, (GLint a, GLsizei b, GLboolean c, const float* d), (a, b, c, d))
GLFN(void, glUniformMatrix4fv, (GLint a, GLsizei b, GLboolean c, const float* d), (a, b, c, d))
GLFN(void, glUseProgram, (GLuint a), (a))
GLFN(void, glValidateProgram, (GLuint a), (a))
GLFN(void, glVertexAttrib1f, (GLuint a, float b), (a, b))
GLFN(void, glVertexAttrib2f, (GLuint a, float b, float c), (a, b, c))
GLFN(void, glVertexAttrib3f, (GLuint a, float b, float c, float d), (a, b, c, d))
GLFN(void, glVertexAttrib4f, (GLuint a, float b, float c, float d, float e), (a, b, c, d, e))
GLFN(void, glVertexAttrib4fv, (GLuint a, const float* b), (a, b))
GLFN(void, glVertexAttribDivisor, (GLuint a, GLuint b), (a, b))
GLFN(void, glVertexAttribPointer, (GLuint a, GLint b, GLenum c, GLboolean d, GLsizei e, const void* f), (a, b, c, d, e, f))
GLFN(void, glViewport, (GLint a, GLint b, GLsizei c, GLsizei d), (a, b, c, d))
GLFN(void*, glMapBufferRange, (GLenum a, GLintptr b, GLsizeiptr c, GLbitfield d), (a, b, c, d))
GLFN(GLboolean, glUnmapBuffer, (GLenum a), (a))
GLFN(void, glFlushMappedBufferRange, (GLenum a, GLintptr b, GLsizeiptr c), (a, b, c))
GLFN(void, glInvalidateFramebuffer, (GLenum a, GLsizei b, const GLenum* c), (a, b, c))
GLFN(void, glTexImage3D, (GLenum a, GLint b, GLint c, GLsizei d, GLsizei e, GLsizei f, GLint g, GLenum h, GLenum i, const void* j), (a, b, c, d, e, f, g, h, i, j))
GLFN(void, glTexSubImage3D, (GLenum a, GLint b, GLint c, GLint d, GLint e, GLsizei f, GLsizei g, GLsizei h, GLenum i, GLenum j, const void* k), (a, b, c, d, e, f, g, h, i, j, k))
GLFN(void, glTexStorage3D, (GLenum a, GLsizei b, GLenum c, GLsizei d, GLsizei e, GLsizei f), (a, b, c, d, e, f))
GLFN(void, glBindBufferBase, (GLenum a, GLuint b, GLuint c), (a, b, c))
GLFN(void, glBindBufferRange, (GLenum a, GLuint b, GLuint c, GLintptr d, GLsizeiptr e), (a, b, c, d, e))
GLFN(GLuint, glGetUniformBlockIndex, (GLuint a, const GLchar* b), (a, b))
GLFN(void, glUniformBlockBinding, (GLuint a, GLuint b, GLuint c), (a, b, c))
GLFN(void, glGenQueries, (GLsizei a, GLuint* b), (a, b))
GLFN(void, glDeleteQueries, (GLsizei a, const GLuint* b), (a, b))
GLFN(void, glBeginQuery, (GLenum a, GLuint b), (a, b))
GLFN(void, glEndQuery, (GLenum a), (a))
GLFN(void, glGetQueryObjectuiv, (GLuint a, GLenum b, GLuint* c), (a, b, c))
GLFN(void, glGenSamplers, (GLsizei a, GLuint* b), (a, b))
GLFN(void, glDeleteSamplers, (GLsizei a, const GLuint* b), (a, b))
GLFN(void, glBindSampler, (GLuint a, GLuint b), (a, b))
GLFN(void, glSamplerParameteri, (GLuint a, GLenum b, GLint c), (a, b, c))
GLFN(void, glFramebufferTextureLayer, (GLenum a, GLenum b, GLuint c, GLint d, GLint e), (a, b, c, d, e))
GLFN(void, glGetInteger64v, (GLenum a, long long* b), (a, b))
GLFN(void, glGetShaderPrecisionFormat, (GLenum a, GLenum b, GLint* c, GLint* d), (a, b, c, d))

GLPTR(void, glCompileShader, (GLuint))
GLPTR(void, glShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))
GLPTR(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*))
GLPTR(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*))
GLPTR(const GLubyte*, glGetString, (GLenum))
GLPTR(const GLubyte*, glGetStringi, (GLenum, GLuint))
GLPTR(void, glGetIntegerv, (GLenum, GLint*))
GLPTR(void*, glMapBuffer, (GLenum, GLenum))

// ---- OES / EXT / APPLE aliases ----
HLE_ALIAS("_glBindRenderbufferOES", hle_impl_glBindRenderbuffer);
HLE_ALIAS("_glGenRenderbuffersOES", hle_impl_glGenRenderbuffers);
HLE_ALIAS("_glDeleteRenderbuffersOES", hle_impl_glDeleteRenderbuffers);
HLE_ALIAS("_glRenderbufferStorageOES", hle_impl_glRenderbufferStorage);
HLE_ALIAS("_glFramebufferRenderbufferOES", hle_impl_glFramebufferRenderbuffer);
HLE_ALIAS("_glBindFramebufferOES", hle_impl_glBindFramebuffer);
HLE_ALIAS("_glGenFramebuffersOES", hle_impl_glGenFramebuffers);
HLE_ALIAS("_glDeleteFramebuffersOES", hle_impl_glDeleteFramebuffers);
HLE_ALIAS("_glCheckFramebufferStatusOES", hle_impl_glCheckFramebufferStatus);
HLE_ALIAS("_glFramebufferTexture2DOES", hle_impl_glFramebufferTexture2D);
HLE_ALIAS("_glGetRenderbufferParameterivOES", hle_impl_glGetRenderbufferParameteriv);
HLE_ALIAS("_glGenerateMipmapOES", hle_impl_glGenerateMipmap);
HLE_ALIAS("_glBindVertexArrayOES", hle_impl_glBindVertexArray);
HLE_ALIAS("_glGenVertexArraysOES", hle_impl_glGenVertexArrays);
HLE_ALIAS("_glDeleteVertexArraysOES", hle_impl_glDeleteVertexArrays);
HLE_ALIAS("_glIsVertexArrayOES", hle_impl_glIsVertexArray);
HLE_ALIAS("_glRenderbufferStorageMultisampleAPPLE", hle_impl_glRenderbufferStorageMultisample);
HLE_ALIAS("_glRenderbufferStorageMultisampleEXT", hle_impl_glRenderbufferStorageMultisample);
HLE_ALIAS("_glTexStorage2DEXT", hle_impl_glTexStorage2D);
HLE_ALIAS("_glUnmapBufferOES", hle_impl_glUnmapBuffer);
HLE_ALIAS("_glMapBufferRangeEXT", hle_impl_glMapBufferRange);
HLE_ALIAS("_glDrawArraysInstancedEXT", hle_impl_glDrawArraysInstanced);
HLE_ALIAS("_glDrawElementsInstancedEXT", hle_impl_glDrawElementsInstanced);
HLE_ALIAS("_glVertexAttribDivisorEXT", hle_impl_glVertexAttribDivisor);

HLE_FUNC(glDiscardFramebufferEXT, void, (GLenum target, GLsizei n, const GLenum* att)) {
    // Desktop drivers gain nothing from it; GL_COLOR_EXT etc. differ from core enums anyway.
    (void)target; (void)n; (void)att;
}
HLE_FUNC(glResolveMultisampleFramebufferAPPLE, void, ()) {}
HLE_FUNC(glMapBufferOES, void*, (GLenum target, GLenum access)) { return p_glMapBuffer ? p_glMapBuffer(target, access) : nullptr; }
HLE_FUNC(glLabelObjectEXT, void, (GLenum, GLuint, GLsizei, const GLchar*)) {}
HLE_FUNC(glPushGroupMarkerEXT, void, (GLsizei, const GLchar*)) {}
HLE_FUNC(glPopGroupMarkerEXT, void, ()) {}
HLE_FUNC(glInsertEventMarkerEXT, void, (GLsizei, const GLchar*)) {}

// ---- strings ----
HLE_FUNC(glGetString, const GLubyte*, (GLenum name)) {
    switch (name) {
    case GL_VENDOR: return (const GLubyte*)"Apple Inc.";
    case GL_RENDERER: return (const GLubyte*)"Apple A9 GPU";
    case GL_VERSION: return (const GLubyte*)"OpenGL ES 2.0 Apple A9 GPU - 95.58";
    case 0x8B8C: return (const GLubyte*)"OpenGL ES GLSL ES 1.00";  // GL_SHADING_LANGUAGE_VERSION
    case GL_EXTENSIONS:
        return (const GLubyte*)"GL_OES_depth24 GL_OES_element_index_uint GL_OES_fbo_render_mipmap GL_OES_mapbuffer "
                               "GL_OES_packed_depth_stencil GL_OES_rgb8_rgba8 GL_OES_standard_derivatives GL_OES_texture_float "
                               "GL_OES_texture_half_float GL_OES_vertex_array_object GL_EXT_blend_minmax GL_EXT_discard_framebuffer "
                               "GL_EXT_draw_instanced GL_EXT_instanced_arrays GL_EXT_map_buffer_range GL_EXT_shader_texture_lod "
                               "GL_EXT_texture_filter_anisotropic GL_EXT_texture_storage GL_APPLE_framebuffer_multisample "
                               "GL_APPLE_texture_max_level GL_EXT_debug_label GL_EXT_debug_marker";
    default: return p_glGetString ? p_glGetString(name) : nullptr;
    }
}

HLE_FUNC(glGetIntegerv, void, (GLenum pname, GLint* out)) {
    if (!p_glGetIntegerv) return;
    p_glGetIntegerv(pname, out);
    switch (pname) {
    case 0x8DFB: if (*out <= 0) *out = 224; break;  // GL_MAX_FRAGMENT_UNIFORM_VECTORS (desktop may report components)
    case 0x8DFC: if (*out <= 0) *out = 15; break;   // GL_MAX_VARYING_VECTORS
    case 0x8DFD: if (*out <= 0) *out = 512; break;  // GL_MAX_VERTEX_UNIFORM_VECTORS
    case 0x8B9A: *out = GL_RGBA; break;             // GL_IMPLEMENTATION_COLOR_READ_TYPE/FORMAT
    case 0x8B9B: *out = GL_UNSIGNED_BYTE; break;
    default: break;
    }
}

// ---- textures: map ES-only formats ----
static void fix_tex_format(GLint& internal, GLenum& format, GLenum& type) {
    if (type == 0x8D61) type = 0x140B;            // HALF_FLOAT_OES -> HALF_FLOAT
    if (format == 0x80E1 && internal == 0x80E1) internal = GL_RGBA;  // BGRA_EXT as internal format
    if (internal == GL_RGBA && type == 0x140B) internal = 0x881A;     // RGBA16F
    if (internal == GL_RGB && type == 0x140B) internal = 0x881B;      // RGB16F
    if (internal == GL_RGBA && type == GL_FLOAT) internal = 0x8814;   // RGBA32F
    if (internal == 0x1902 && format == 0x1902) {                    // DEPTH_COMPONENT
        if (type == GL_UNSIGNED_SHORT) internal = 0x81A5;            // DEPTH_COMPONENT16
        else if (type == GL_UNSIGNED_INT) internal = 0x81A6;         // DEPTH_COMPONENT24
    }
    if (internal == 0x84F9 && format == 0x84F9) internal = 0x88F0;   // DEPTH_STENCIL -> DEPTH24_STENCIL8
}
HLE_FUNC(glTexImage2D, void, (GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void* px)) {
    fix_tex_format(internal, format, type);
    if (p_glTexImage2D) p_glTexImage2D(target, level, internal, w, h, border, format, type, px);
}
HLE_FUNC(glTexSubImage2D, void, (GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, const void* px)) {
    GLint dummy = 0;
    fix_tex_format(dummy, format, type);
    if (p_glTexSubImage2D) p_glTexSubImage2D(target, level, x, y, w, h, format, type, px);
}

// ---- shaders: GLSL ES on desktop GL ----
namespace {
std::mutex g_shader_lock;
std::unordered_map<GLuint, std::string> g_shader_src;

std::string translate_glsl_es(const std::string& src) {
    // Fallback for drivers without GL_ARB_ES2_compatibility "#version 100" support.
    std::string out;
    size_t pos = 0;
    bool versioned = false;
    while (pos < src.size()) {
        size_t e = src.find('\n', pos);
        if (e == std::string::npos) e = src.size();
        std::string line = src.substr(pos, e - pos);
        pos = e + 1;
        size_t f = line.find_first_not_of(" \t");
        std::string t = f == std::string::npos ? "" : line.substr(f);
        if (t.rfind("#version", 0) == 0) {
            bool es3 = t.find("300") != std::string::npos;
            out += es3 ? "#version 330\n" : "#version 120\n";
            out += "#define lowp\n#define mediump\n#define highp\n";
            versioned = true;
            continue;
        }
        if (t.rfind("precision ", 0) == 0) continue;
        out += line + "\n";
    }
    if (!versioned) out = "#version 120\n#define lowp\n#define mediump\n#define highp\n" + out;
    return out;
}
}  // namespace

HLE_FUNC(glShaderSource, void, (GLuint shader, GLsizei count, const GLchar* const* strings, const GLint* lengths)) {
    std::string src;
    for (GLsizei i = 0; i < count; i++) {
        const GLchar* s = gptr<const GLchar>(gaddr_of(strings[i]));
        if (!s) continue;
        if (lengths && lengths[i] >= 0) src.append(s, lengths[i]);
        else src.append(s);
    }
    if (src.find("#version") == std::string::npos) src = "#version 100\n" + src;
    {
        std::lock_guard lk(g_shader_lock);
        g_shader_src[shader] = src;
    }
    const GLchar* p = src.c_str();
    if (p_glShaderSource) p_glShaderSource(shader, 1, &p, nullptr);
}

// Compile with a translation fallback when the driver rejects GLSL ES.
static void compile_with_fallback(GLuint shader) {
    if (!p_glCompileShader) return;
    p_glCompileShader(shader);
    GLint ok = 0;
    p_glGetShaderiv(shader, 0x8B81, &ok);  // GL_COMPILE_STATUS
    if (ok) return;
    std::string src;
    {
        std::lock_guard lk(g_shader_lock);
        auto it = g_shader_src.find(shader);
        if (it == g_shader_src.end()) return;
        src = it->second;
    }
    char log[2048] = {0};
    p_glGetShaderInfoLog(shader, sizeof log, nullptr, log);
    std::string t = translate_glsl_es(src);
    const GLchar* p = t.c_str();
    p_glShaderSource(shader, 1, &p, nullptr);
    p_glCompileShader(shader);
    p_glGetShaderiv(shader, 0x8B81, &ok);
    if (!ok) {
        char log2[2048] = {0};
        p_glGetShaderInfoLog(shader, sizeof log2, nullptr, log2);
        LOGW("shader %u failed to compile:\n%s\n(translated: %s)", shader, log, log2);
    }
}
static ::hle::FuncRegistrar g_reg_compile("_glCompileShader", [](Cpu& c) { compile_with_fallback((GLuint)c.x(0)); }, false);

// ======================================================================== host services for guest UIKit
namespace {
GLuint g_drawable_rb = 0;
int g_drawable_w = 0, g_drawable_h = 0;
}  // namespace

HLE_FUNC(emu_window_create, int, (int w, int h)) {
    if (!window::create(w, h, "Minecraft PE 0.16.2 (iOS)")) return 0;
    load_gl();
    g_gl_trace = getenv("EMU_GL_TRACE") != nullptr;
    return 1;
}
HLE_FUNC(emu_window_size, void, (int* w, int* h)) { window::size(w, h); }
namespace patches {
void game_resized(int w, int h);
bool game_input_ready();
bool in_world();
void game_key(int key, bool down);
void game_mouse(int action, int data, int x, int y, int dx, int dy);
}
// Runs on the guest main thread (the game's input thread), so it may feed the game's
// keyboard/mouse queues directly; reentrant because growing them calls guest code.
HLE_FUNC_R(emu_poll_events, int, (window::Event* out, int max)) {
    static const bool keys_to_game = getenv("EMU_NO_GAME_KEYS") == nullptr;
    static bool touch_down = false;
    // Events held back until the next frame: the game resolves what is under the
    // cursor once per frame, so a click must not arrive in the same frame as the
    // cursor movement that brought the cursor onto the button / slot / list.
    static std::deque<window::Event> carry;
    static u64 pos_frame = ~0ull;
    if (max > 64) max = 64;
    window::Event in[64];
    int n = 0;
    while (n < max && !carry.empty()) {
        in[n++] = carry.front();
        carry.pop_front();
    }
    n += window::poll(in + n, max - n);
    int w = 0, h = 0;
    window::size(&w, &h);
    const bool ready = patches::game_input_ready();
    int m = 0;
    for (int i = 0; i < n; i++) {
        const window::Event e = in[i];
        switch (e.type) {
        case window::EvGameMouseMove:
            if (ready) {
                // sensitivity from controls.ini; keep the fractional part for the next event
                static float rx = 0, ry = 0;
                float sens = controls::mouse_sensitivity();
                rx += e.x * sens;
                ry += e.y * sens * (controls::invert_mouse_y() ? -1.0f : 1.0f);
                int dx = (int)rx, dy = (int)ry;
                rx -= dx;
                ry -= dy;
                dx = (std::max)(-32000, (std::min)(32000, dx));
                dy = (std::max)(-32000, (std::min)(32000, dy));
                if (dx || dy) patches::game_mouse(0, 0, w / 2, h / 2, dx, dy);
            }
            continue;  // host-side only
        case window::EvGameMousePos:
            if (ready) {
                patches::game_mouse(0, 0, (int)e.x, (int)e.y, 0, 0);
                pos_frame = window::frames_presented();
                continue;
            }
            if (!touch_down) continue;  // before the game's input exists: emulate a touch
            out[m++] = {window::EvPointerMove, 0, e.x, e.y, 0, 0, e.time};
            continue;
        case window::EvGameMouseButton:
            if (ready) {
                if (pos_frame == window::frames_presented()) {  // let the game see the cursor first
                    for (int k = n - 1; k >= i; k--) carry.push_front(in[k]);
                    return m;
                }
                if (e.id == 4) {
                    // wheel: lists scroll by data / 127 per event, so a notch sends a full-scale
                    // event (twice by default on screens); in the world one event = one hotbar slot
                    int dir = (s32)e.chr > 0 ? 1 : -1;
                    float amount = patches::in_world() ? 1.0f : 2.0f * controls::scroll_speed();
                    while (amount > 0.01f) {
                        float a = (std::min)(amount, 1.0f);
                        patches::game_mouse(4, dir * (std::max)(1, (int)(127 * a + 0.5f)), (int)e.x, (int)e.y, 0, 0);
                        amount -= a;
                    }
                } else {
                    patches::game_mouse(e.id, (s8)(s32)e.chr, (int)e.x, (int)e.y, 0, 0);
                }
                continue;
            }
            if (e.id != 1) continue;
            touch_down = e.chr != 0;
            out[m++] = {touch_down ? window::EvPointerDown : window::EvPointerUp, 0, e.x, e.y, 0, 0, e.time};
            continue;
        case window::EvResize:
            out[m++] = e;  // the guest updates UIScreen / the views
            patches::game_resized((int)e.x, (int)e.y);
            continue;
        case window::EvKeyDown:
        case window::EvKeyUp:
            // keys go to the game unless a text field has them (Esc always closes the screen)
            if (keys_to_game && ready && !(e.mods & 0x100) && (!window::text_input_active() || e.id == 0x1b))
                patches::game_key(e.id, e.type == window::EvKeyDown);
            break;
        default: break;
        }
        out[m++] = e;
    }
    return m;
}
HLE_FUNC(emu_wait_events, void, (double seconds)) { window::wait(seconds); }
HLE_FUNC(emu_time, double, ()) { return window::now(); }
HLE_FUNC(emu_frame_interval, double, ()) {
    // While a phone is connected the game runs at 60 fps like on iOS: some touch gestures
    // count frames (the sneak button's double tap must come within ~24 frames, which is
    // 0.4 s at 60 fps but only 0.15 s at 165 fps), and the phone is sent at most 60 fps anyway.
    if (window::phone_connected()) return 1.0 / 60.0;
    int fps = controls::max_fps();
    if (fps < 0) fps = window::refresh_rate();
    return fps > 0 ? 1.0 / fps : 0.0;
}
HLE_FUNC(emu_set_title, void, (const char* t)) { window::set_title(t); }
HLE_FUNC(emu_set_text_input, void, (int on)) { window::set_text_input(on != 0); }
HLE_FUNC(emu_message_box, int, (const char* title, const char* text, int buttons)) { return window::message_box(title, text, buttons); }

// [EAGLContext renderbufferStorage:fromDrawable:] - storage for the bound renderbuffer
HLE_FUNC(emu_gl_storage_from_drawable, int, (int w, int h)) {
    if (!p_glGetIntegerv || !p_glRenderbufferStorage) return 0;
    GLint rb = 0;
    p_glGetIntegerv(0x8CA7, &rb);  // GL_RENDERBUFFER_BINDING
    p_glRenderbufferStorage(0x8D41, 0x8058, w, h);  // GL_RENDERBUFFER, GL_RGBA8
    g_drawable_rb = (GLuint)rb;
    g_drawable_w = w;
    g_drawable_h = h;
    LOGI("drawable storage: renderbuffer %d %dx%d", rb, w, h);
    return 1;
}
// [EAGLContext presentRenderbuffer:]
HLE_FUNC(emu_gl_present, int, ()) {
    GLint rb = 0;
    if (p_glGetIntegerv) p_glGetIntegerv(0x8CA7, &rb);
    GLuint target = g_drawable_rb ? g_drawable_rb : (GLuint)rb;
    if (!target) return 0;
    window::present(target, g_drawable_w, g_drawable_h);
    return 1;
}
