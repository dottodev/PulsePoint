// The slice of OpenGL ES 3.0 that PulsePoint uses, declared locally.
//
// On Android these symbols are exported by libGLESv3.so, so declaring them here
// and linking -lGLESv3 is all that is needed -- no loader, no header search
// path, no generated bindings.  Declaring them locally also means every file
// that draws something can be syntax-checked on a desktop, which is how the
// renderer is kept honest without a device.
#pragma once

#include <cstddef>
#include <cstdint>

// Types and constants are declared at global scope, matching the Khronos
// headers, so call sites read exactly like ordinary GLES code.
using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLfloat = float;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLchar = char;
using GLintptr = intptr_t;
using GLsizeiptr = ptrdiff_t;

constexpr GLenum GL_FALSE = 0;
constexpr GLenum GL_TRUE = 1;


constexpr GLenum GL_COLOR_BUFFER_BIT = 0x00004000;
constexpr GLenum GL_DEPTH_BUFFER_BIT = 0x00000100;
constexpr GLenum GL_STENCIL_BUFFER_BIT = 0x00000400;

constexpr GLenum GL_TRIANGLES = 0x0004;
constexpr GLenum GL_UNSIGNED_SHORT = 0x1403;
constexpr GLenum GL_UNSIGNED_INT = 0x1405;
constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum GL_FLOAT = 0x1406;

constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
constexpr GLenum GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_INFO_LOG_LENGTH = 0x8B84;

constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER = 0x8893;
constexpr GLenum GL_STREAM_DRAW = 0x88E0;
constexpr GLenum GL_STATIC_DRAW = 0x88E4;
constexpr GLenum GL_DYNAMIC_DRAW = 0x88E8;

constexpr GLenum GL_FRAGMENT_SHADER_BIT = 0x00000040;
constexpr GLenum GL_VERTEX_SHADER_BIT = 0x00000001;

constexpr GLenum GL_BLEND = 0x0BE2;
constexpr GLenum GL_SRC_ALPHA = 0x0302;
constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr GLenum GL_ONE = 1;
constexpr GLenum GL_FUNC_ADD = 0x8006;

constexpr GLenum GL_CULL_FACE = 0x0B44;
constexpr GLenum GL_STENCIL_TEST = 0x0B90;
constexpr GLenum GL_DEPTH_TEST = 0x0B71;
constexpr GLenum GL_SCISSOR_TEST = 0x0C11;
constexpr GLenum GL_BACK = 0x0405;

constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_TEXTURE0 = 0x84C0;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
constexpr GLenum GL_LINEAR = 0x2601;
constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_R8 = 0x8229;
constexpr GLenum GL_RED = 0x1903;
constexpr GLenum GL_UNPACK_ALIGNMENT = 0x0CF5;
constexpr GLenum GL_ALPHA = 0x1906;

// The ES entry points themselves live in the global namespace, exactly as the
// Khronos headers declare them.
extern "C" {

GLenum glGetError();
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glClear(GLbitfield mask);
void glEnable(GLenum cap);
void glDisable(GLenum cap);
void glBlendFunc(GLenum src, GLenum dst);
void glBlendEquation(GLenum mode);
void glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcA, GLenum dstA);
void glCullFace(GLenum mode);
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h);

GLuint glCreateShader(GLenum type);
void glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string,
                    const GLint* length);
void glCompileShader(GLuint shader);
void glGetShaderiv(GLuint shader, GLenum pname, GLint* params);
void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
void glDeleteShader(GLuint shader);

GLuint glCreateProgram();
void glAttachShader(GLuint program, GLuint shader);
void glLinkProgram(GLuint program);
void glGetProgramiv(GLuint program, GLenum pname, GLint* params);
void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
void glUseProgram(GLuint program);
void glDeleteProgram(GLuint program);
GLint glGetUniformLocation(GLuint program, const GLchar* name);
void glUniform1f(GLint location, GLfloat v);
void glUniform1i(GLint location, GLint v);
void glUniform2f(GLint location, GLfloat x, GLfloat y);
void glUniform4f(GLint location, GLfloat x, GLfloat y, GLfloat z, GLfloat w);

void glGenBuffers(GLsizei n, GLuint* buffers);
void glDeleteBuffers(GLsizei n, const GLuint* buffers);
void glBindBuffer(GLenum target, GLuint buffer);
void glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage);

void glGenVertexArrays(GLsizei n, GLuint* arrays);
void glDeleteVertexArrays(GLsizei n, const GLuint* arrays);
void glBindVertexArray(GLuint array);
void glEnableVertexAttribArray(GLuint index);
void glDisableVertexAttribArray(GLuint index);
void glVertexAttribPointer(GLuint index, GLsizei size, GLenum type, GLboolean normalized,
                           GLsizei stride, const void* pointer);

void glGenTextures(GLsizei n, GLuint* textures);
void glDeleteTextures(GLsizei n, const GLuint* textures);
void glBindTexture(GLenum target, GLuint texture);
void glActiveTexture(GLenum texture);
void glTexParameteri(GLenum target, GLenum pname, GLint param);
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width,
                  GLsizei height, GLint border, GLenum format, GLenum type,
                  const void* pixels);
void glPixelStorei(GLenum pname, GLint param);

void glDrawArrays(GLenum mode, GLint first, GLsizei count);
}  // extern "C"
