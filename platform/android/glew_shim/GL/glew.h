#pragma once

/// \file GL/glew.h (shim)
/// \brief Minimal GLEW compatibility header for Android (GLES 3.0) builds.
///
/// The engine sources include `<GL/glew.h>` and use desktop-GL entry points
/// resolved at runtime by GLEW. On Android there is no GLEW: GLES 3.0 symbols
/// are provided directly by libGLESv2.so, so this header simply includes the
/// GLES3 headers and re-exposes everything through the `GL/glew.h` path.
///
/// Desktop-GL constants/functions that do not exist in GLES3 (debug output,
/// blitFramebuffer, ...) are defined here as no-op stubs or with their desktop
/// numeric values, so shared rendering code compiles unchanged.
///
/// NOTE: this directory must be added to the include path BEFORE any real
/// `GL/glew.h` (see src/CMakeLists.txt ANDROID branch).

#if !defined(__ANDROID__) && !defined(VC_GLEW_SHIM)
#error "GL/glew.h shim must only be used on Android / VC_GLEW_SHIM builds"
#endif

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- GLEW API surface -------------------------------------------------------

#define GLEW_OK 0

/// GLEW "experimental" mode has no meaning under GLES; kept as a no-op symbol.
extern int glewExperimental;

/// Returns GLEW_OK. All GLES3 entry points are statically available through
/// libGLESv2, so there is nothing to initialize.
static inline unsigned int glewInit(void) {
    return GLEW_OK;
}

static inline const char* glewGetErrorString(unsigned int /*err*/) {
    return "GLES3 shim: no errors";
}

// ---- Desktop-only enums missing in GLES3 ------------------------------------

#ifndef GL_DEBUG_OUTPUT
#define GL_DEBUG_OUTPUT 0x92E0
#endif
#ifndef GL_DEBUG_OUTPUT_SYNCHRONOUS
#define GL_DEBUG_OUTPUT_SYNCHRONOUS 0x8242
#endif
#ifndef GL_DEBUG_TYPE_ERROR
#define GL_DEBUG_TYPE_ERROR 0x824C
#endif
#ifndef GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR
#define GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR 0x824D
#endif
#ifndef GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR
#define GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR 0x824E
#endif
#ifndef GL_DEBUG_TYPE_PORTABILITY
#define GL_DEBUG_TYPE_PORTABILITY 0x824F
#endif
#ifndef GL_DEBUG_TYPE_PERFORMANCE
#define GL_DEBUG_TYPE_PERFORMANCE 0x8250
#endif
#ifndef GL_DEBUG_TYPE_OTHER
#define GL_DEBUG_TYPE_OTHER 0x8251
#endif
#ifndef GL_DEBUG_SEVERITY_HIGH
#define GL_DEBUG_SEVERITY_HIGH 0x9146
#endif
#ifndef GL_DEBUG_SEVERITY_MEDIUM
#define GL_DEBUG_SEVERITY_MEDIUM 0x9147
#endif
#ifndef GL_DEBUG_SEVERITY_LOW
#define GL_DEBUG_SEVERITY_LOW 0x8268
#endif
#ifndef GL_DEBUG_SEVERITY_NOTIFICATION
#define GL_DEBUG_SEVERITY_NOTIFICATION 0x826B
#endif
#ifndef GL_TEXTURE_BORDER_COLOR
#define GL_TEXTURE_BORDER_COLOR 0x1004
#endif
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER 0x812D
#endif
#ifndef GL_COMPARE_REF_TO_TEXTURE
#define GL_COMPARE_REF_TO_TEXTURE 0x8803
#endif
#ifndef GL_RGB16F
#define GL_RGB16F 0x881B
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#endif

#ifndef GLAPIENTRY
#define GLAPIENTRY
#endif

// KHR_debug callback type (may be absent on some NDK headers)
#ifndef GLDEBUGPROCKHR_defined
#if !defined(GL_DEBUG_LOGGED_MESSAGES)
typedef void(GLAPIENTRY* PFNGlDebugProc)(GLenum source, GLenum type,
                                         GLuint id, GLenum severity,
                                         GLsizei length, const GLchar* message,
                                         const void* userParam);
#define GL_DEBUG_LOGGED_MESSAGES 0x9141
#else
typedef void(GLAPIENTRY* PFNGlDebugProc)(GLenum source, GLenum type,
                                         GLuint id, GLenum severity,
                                         GLsizei length, const GLchar* message,
                                         const void* userParam);
#endif
#define GLDEBUGPROCKHR PFNGlDebugProc
#endif

// ---- Desktop-only functions missing in GLES3 --------------------------------
// Implemented in platform/android/glew_shim/glew_shim.cpp as weak symbols, so
// a real extension loader can override them if present at link time.

void glDebugMessageCallback(GLDEBUGPROCKHR callback, const void* userParam);
void glDebugMessageControl(GLenum source, GLenum type, GLenum severity,
                           GLsizei count, const GLuint* ids,
                           GLboolean enabled);

// glBlitFramebuffer does not exist in GLES3 core. Calls are guarded in the
// engine code; provide an aborting stub so accidental use fails loudly.
void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                       GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                       GLbitfield mask, GLenum filter);

#ifdef __cplusplus
}
#endif
