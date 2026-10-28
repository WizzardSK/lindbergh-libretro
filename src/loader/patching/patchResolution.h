#pragma once

#include <GL/gl.h>
#include <stdint.h>
#include <stdio.h>

void glu_Ortho2D(double left, double right, double bottom, double top);
int hummerRespatch();
void gl_TexImage2DOR(unsigned int target, int level, int internalformat, int width, int height, int border,
                     unsigned int format, unsigned int type, const void *pixels);
size_t harleyFreadReplace(void *buf, size_t size, size_t count, FILE *stream);
void glTexCoord2fvABC1(const float *v);
void glVertex3fABC1(float x, float y, float z);
void glVertex3fABC2(float x, float y, float z);
int isTestMode();

typedef struct
{
    float x;
    float y;
    float z;
} vertexABC;

typedef struct
{
    float m00;
    float m01;
    float m02;
    float m03;
    float m10;
    float m11;
    float m12;
    float m13;
    float m20;
    float m21;
    float m22;
    float m23;
    float m30;
    float m31;
    float m32;
    float m33;
} Matrix4f;

typedef struct
{
    uint32_t flag;
    uint32_t id;
    uint32_t color;
    vertexABC center;
    vertexABC trans;
    vertexABC scale;
    char rect[0x10];
    vertexABC rot;
    Matrix4f mat;
} SprArgs;


#ifdef __cplusplus
extern "C" {
#endif
int initResolutionPatches();
// Windows functions
void bridgeglOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
void bridgeglCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y,
                               GLsizei width, GLsizei height);
void bridgeglViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void bridgeglTexImage2D (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border,
                        GLenum format, GLenum type, const void *pixels);
void bridgeglTexParameteri(GLenum target, GLenum pname, GLint param);
void myGlBindTexture(unsigned int target, unsigned int texture);
// Call after uploading a compressed texture level, see patchResolution.c.
void gsevoBleedAlpha(unsigned int target, int level, unsigned int internalformat, int width, int height);

#ifdef __cplusplus
}
#endif