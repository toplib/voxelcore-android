/// \file glew_shim.cpp — weak fallback implementations of desktop-GL entry
/// points referenced by shared engine code but unavailable in GLES3.

#if defined(__ANDROID__) || defined(VC_GLEW_SHIM)

#include <GL/glew.h>

#include <cassert>

int glewExperimental = 0;

extern "C" {

__attribute__((weak)) void glDebugMessageCallback(GLDEBUGPROCKHR,
                                                  const void*) {
    // debug output is a desktop-GL feature; silently ignored on GLES3
}

__attribute__((weak)) void glDebugMessageControl(GLenum, GLenum, GLenum,
                                                 GLsizei, const GLuint*,
                                                 GLboolean) {
}

__attribute__((weak)) void glBlitFramebuffer(GLint, GLint, GLint, GLint,
                                             GLint, GLint, GLint, GLint,
                                             GLbitfield, GLenum) {
    assert(false && "glBlitFramebuffer is not supported on GLES3");
}

}  // extern "C"

#endif
