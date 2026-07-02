#pragma once
#include <stdint.h>

/*
 * Real (software) immediate-mode OpenGL-style fixed-function subset for
 * this freestanding kernel. There is no GPU driver here, so unlike a real
 * GL implementation this is a from-scratch CPU pipeline: object space ->
 * eye space (modelview) -> clip space (projection) -> perspective divide
 * -> viewport transform -> triangle rasterization (reusing the same
 * barycentric-coverage technique already proven in src/nvg_backend.c),
 * with a real per-pixel depth buffer and alpha blending.
 *
 * This is NOT a conformant OpenGL or OpenGL ES implementation -- it's a
 * deliberately small, genuinely-working subset covering the common
 * fixed-function path (immediate-mode glBegin/glVertex, matrix stack,
 * single-texture modulate, standard alpha blending). Known gaps, not
 * silently faked:
 *   - No lighting (glNormal3f is accepted and ignored)
 *   - No display lists, no vertex buffer objects, no shaders
 *   - glBlendFunc only really distinguishes "standard alpha blend" vs
 *     "replace" -- other src/dst factor combinations fall back to alpha
 *     blend rather than being computed exactly
 *   - Texturing is single-unit, GL_MODULATE only, nearest or bilinear
 */

typedef float          GLfloat;
typedef double          GLdouble;
typedef int             GLint;
typedef int             GLsizei;
typedef unsigned int    GLuint;
typedef unsigned int    GLenum;
typedef unsigned int    GLbitfield;
typedef unsigned char   GLubyte;
typedef unsigned char   GLboolean;
typedef void             GLvoid;

#define GL_FALSE 0
#define GL_TRUE  1

/* Primitive modes */
#define GL_POINTS         0x0000
#define GL_LINES          0x0001
#define GL_LINE_STRIP     0x0003
#define GL_TRIANGLES      0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN   0x0006
#define GL_QUADS          0x0007

/* glClear bits */
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000

/* Matrix modes */
#define GL_MODELVIEW  0x1700
#define GL_PROJECTION 0x1701

/* Capabilities */
#define GL_TEXTURE_2D  0x0DE1
#define GL_BLEND       0x0BE2
#define GL_DEPTH_TEST  0x0B71
#define GL_CULL_FACE   0x0B44

/* Blend factors (recognized subset) */
#define GL_ZERO                0x0000
#define GL_ONE                 0x0001
#define GL_SRC_ALPHA           0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303

/* Depth funcs */
#define GL_LESS  0x0201
#define GL_ALWAYS 0x0207

/* Texture params */
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_NEAREST 0x2600
#define GL_LINEAR  0x2601

/* glTexImage2D format/type (subset) */
#define GL_RGBA          0x1908
#define GL_UNSIGNED_BYTE 0x1401

/* ── Context lifecycle ──────────────────────────────────────────────────── */
void gl_elseaos_init(uint32_t* canvas, int width, int height);
void gl_elseaos_set_canvas(uint32_t* canvas, int width, int height);

/* ── State ──────────────────────────────────────────────────────────────── */
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glClear(GLbitfield mask);
void glEnable(GLenum cap);
void glDisable(GLenum cap);
void glBlendFunc(GLenum sfactor, GLenum dfactor);
void glDepthFunc(GLenum func);
void glDepthMask(GLboolean flag);
void glFlush(void);
void glFinish(void);
const GLubyte* glGetString(GLenum name);

/* ── Matrix stack ───────────────────────────────────────────────────────── */
void glMatrixMode(GLenum mode);
void glLoadIdentity(void);
void glPushMatrix(void);
void glPopMatrix(void);
void glLoadMatrixf(const GLfloat* m);
void glMultMatrixf(const GLfloat* m);
void glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void glScalef(GLfloat x, GLfloat y, GLfloat z);
void glRotatef(GLfloat angle_deg, GLfloat x, GLfloat y, GLfloat z);
void glOrthof(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f);
void glFrustumf(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f);

/* ── Immediate mode ─────────────────────────────────────────────────────── */
void glBegin(GLenum mode);
void glEnd(void);
void glVertex2f(GLfloat x, GLfloat y);
void glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void glColor3f(GLfloat r, GLfloat g, GLfloat b);
void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glColor3ub(GLubyte r, GLubyte g, GLubyte b);
void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a);
void glTexCoord2f(GLfloat u, GLfloat v);
void glNormal3f(GLfloat x, GLfloat y, GLfloat z); /* accepted, ignored -- no lighting */

/* ── Textures ───────────────────────────────────────────────────────────── */
void glGenTextures(GLsizei n, GLuint* textures);
void glDeleteTextures(GLsizei n, const GLuint* textures);
void glBindTexture(GLenum target, GLuint texture);
void glTexImage2D(GLenum target, GLint level, GLint internalformat,
                   GLsizei width, GLsizei height, GLint border,
                   GLenum format, GLenum type, const void* data);
void glTexParameteri(GLenum target, GLenum pname, GLint param);
