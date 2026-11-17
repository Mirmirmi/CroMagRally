// Minimal OpenGL 1.x fixed-function to OpenGL ES 3.0 / WebGL2 shim
// Used on Emscripten builds so legacy GL calls run unmodified on WebGL2.

#pragma once

#ifdef __EMSCRIPTEN__

#include <GLES3/gl3.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef GLfloat GLclampf;
typedef double  GLdouble;

#ifndef GL_MODELVIEW
#define GL_MODELVIEW                    0x1700
#endif
#ifndef GL_PROJECTION
#define GL_PROJECTION                   0x1701
#endif
#ifndef GL_QUADS
#define GL_QUADS                        0x0007
#endif
#ifndef GL_VERTEX_ARRAY
#define GL_VERTEX_ARRAY                 0x8074
#endif
#ifndef GL_NORMAL_ARRAY
#define GL_NORMAL_ARRAY                 0x8075
#endif
#ifndef GL_COLOR_ARRAY
#define GL_COLOR_ARRAY                  0x8076
#endif
#ifndef GL_TEXTURE_COORD_ARRAY
#define GL_TEXTURE_COORD_ARRAY          0x8078
#endif
#ifndef GL_LIGHTING
#define GL_LIGHTING                     0x0B50
#endif
#ifndef GL_LIGHT0
#define GL_LIGHT0                       0x4000
#endif
#ifndef GL_POSITION
#define GL_POSITION                     0x1203
#endif
#ifndef GL_AMBIENT
#define GL_AMBIENT                      0x1200
#endif
#ifndef GL_DIFFUSE
#define GL_DIFFUSE                      0x1201
#endif
#ifndef GL_AMBIENT_AND_DIFFUSE
#define GL_AMBIENT_AND_DIFFUSE          0x1602
#endif
#ifndef GL_LIGHT_MODEL_AMBIENT
#define GL_LIGHT_MODEL_AMBIENT          0x0B53
#endif
#ifndef GL_COLOR_MATERIAL
#define GL_COLOR_MATERIAL               0x0B57
#endif
#ifndef GL_NORMALIZE
#define GL_NORMALIZE                    0x0BA1
#endif
#ifndef GL_RESCALE_NORMAL
#define GL_RESCALE_NORMAL               0x803A
#endif
#ifndef GL_ALPHA_TEST
#define GL_ALPHA_TEST                   0x0BC0
#endif
#ifndef GL_FOG
#define GL_FOG                          0x0B60
#endif
#ifndef GL_FOG_MODE
#define GL_FOG_MODE                     0x0B65
#endif
#ifndef GL_FOG_DENSITY
#define GL_FOG_DENSITY                  0x0B62
#endif
#ifndef GL_FOG_START
#define GL_FOG_START                    0x0B63
#endif
#ifndef GL_FOG_END
#define GL_FOG_END                      0x0B64
#endif
#ifndef GL_FOG_COLOR
#define GL_FOG_COLOR                    0x0B66
#endif
#ifndef GL_FOG_HINT
#define GL_FOG_HINT                     0x0C54
#endif
#ifndef GL_CLIP_VOLUME_CLIPPING_HINT_EXT
#define GL_CLIP_VOLUME_CLIPPING_HINT_EXT 0x80F0
#endif
#ifndef GL_NICEST
#define GL_NICEST                       0x1102
#endif
#ifndef GL_FASTEST
#define GL_FASTEST                      0x1101
#endif
#ifndef GL_CURRENT_COLOR
#define GL_CURRENT_COLOR                0x0B00
#endif
#ifndef GL_BLEND_SRC
#define GL_BLEND_SRC                    0x0BE1
#endif
#ifndef GL_BLEND_DST
#define GL_BLEND_DST                    0x0BE0
#endif
#ifndef GL_BGRA
#define GL_BGRA                         0x80E1
#endif
#ifndef GL_UNSIGNED_SHORT_1_5_5_5_REV
#define GL_UNSIGNED_SHORT_1_5_5_5_REV   0x8366
#endif
#ifndef GL_RGB5_A1
#define GL_RGB5_A1                      0x8057
#endif
#ifndef GL_FILL
#define GL_FILL                         0x1B02
#endif
#ifndef GL_LINE
#define GL_LINE                         0x1B01
#endif
#ifndef GL_EXP
#define GL_EXP                          0x0800
#endif
#ifndef GL_EXP2
#define GL_EXP2                         0x0801
#endif

void shim_glEnable(GLenum cap);
void shim_glDisable(GLenum cap);
GLboolean shim_glIsEnabled(GLenum cap);
void shim_glGetIntegerv(GLenum pname, GLint* data);
void shim_glGetFloatv(GLenum pname, GLfloat* data);
void shim_glBlendFunc(GLenum sfactor, GLenum dfactor);
void shim_glTexImage2D(GLenum target, GLint level, GLint internalformat,
                       GLsizei width, GLsizei height, GLint border,
                       GLenum format, GLenum type, const void* pixels);

void shim_glMatrixMode(GLenum mode);
void shim_glLoadIdentity(void);
void shim_glPushMatrix(void);
void shim_glPopMatrix(void);
void shim_glLoadMatrixf(const GLfloat* m);
void shim_glMultMatrixf(const GLfloat* m);
void shim_glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble nearVal, GLdouble farVal);
void shim_glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void shim_glScalef(GLfloat x, GLfloat y, GLfloat z);
void shim_glRotatef(GLfloat angleDeg, GLfloat x, GLfloat y, GLfloat z);

void shim_glColor3f(GLfloat red, GLfloat green, GLfloat blue);
void shim_glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void shim_glColor4fv(const GLfloat* v);
void shim_glTexCoord2f(GLfloat s, GLfloat t);
void shim_glVertex2f(GLfloat x, GLfloat y);
void shim_glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void shim_glBegin(GLenum mode);
void shim_glEnd(void);

void shim_glEnableClientState(GLenum array);
void shim_glDisableClientState(GLenum array);
void shim_glVertexPointer(GLint size, GLenum type, GLsizei stride, const void* pointer);
void shim_glNormalPointer(GLenum type, GLsizei stride, const void* pointer);
void shim_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void* pointer);
void shim_glColorPointer(GLint size, GLenum type, GLsizei stride, const void* pointer);
void shim_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices);

void shim_glAlphaFunc(GLenum func, GLclampf ref);
void shim_glFogi(GLenum pname, GLint param);
void shim_glFogf(GLenum pname, GLfloat param);
void shim_glFogfv(GLenum pname, const GLfloat* params);
void shim_glLightModelfv(GLenum pname, const GLfloat* params);
void shim_glLightfv(GLenum light, GLenum pname, const GLfloat* params);
void shim_glMaterialfv(GLenum face, GLenum pname, const GLfloat* params);
void shim_glColorMaterial(GLenum face, GLenum mode);
void shim_glHint(GLenum target, GLenum mode);
void shim_glPolygonMode(GLenum face, GLenum mode);
void shim_glShutdown(void);

#ifndef OGL_GLES3_SHIM_IMPL
#define glEnable            shim_glEnable
#define glDisable           shim_glDisable
#define glIsEnabled         shim_glIsEnabled
#define glGetIntegerv       shim_glGetIntegerv
#define glGetFloatv         shim_glGetFloatv
#define glBlendFunc         shim_glBlendFunc
#define glTexImage2D        shim_glTexImage2D

#define glMatrixMode        shim_glMatrixMode
#define glLoadIdentity      shim_glLoadIdentity
#define glPushMatrix        shim_glPushMatrix
#define glPopMatrix         shim_glPopMatrix
#define glLoadMatrixf       shim_glLoadMatrixf
#define glMultMatrixf       shim_glMultMatrixf
#define glOrtho             shim_glOrtho
#define glTranslatef        shim_glTranslatef
#define glScalef            shim_glScalef
#define glRotatef           shim_glRotatef

#define glColor3f           shim_glColor3f
#define glColor4f           shim_glColor4f
#define glColor4fv          shim_glColor4fv
#define glTexCoord2f        shim_glTexCoord2f
#define glVertex2f          shim_glVertex2f
#define glVertex3f          shim_glVertex3f
#define glBegin             shim_glBegin
#define glEnd               shim_glEnd

#define glEnableClientState     shim_glEnableClientState
#define glDisableClientState    shim_glDisableClientState
#define glVertexPointer         shim_glVertexPointer
#define glNormalPointer         shim_glNormalPointer
#define glTexCoordPointer       shim_glTexCoordPointer
#define glColorPointer          shim_glColorPointer
#define glDrawElements          shim_glDrawElements

#define glAlphaFunc         shim_glAlphaFunc
#define glFogi              shim_glFogi
#define glFogf              shim_glFogf
#define glFogfv             shim_glFogfv
#define glLightModelfv      shim_glLightModelfv
#define glLightfv           shim_glLightfv
#define glMaterialfv        shim_glMaterialfv
#define glColorMaterial     shim_glColorMaterial
#define glHint              shim_glHint
#define glPolygonMode       shim_glPolygonMode
#define glLockArraysEXT(a, b)   ((void)0)
#define glUnlockArraysEXT()     ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif // __EMSCRIPTEN__
