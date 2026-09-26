// Adapted from adeism/OSkate arena/01a0cbce-oskate (commit 4bdcb144).
#pragma once
#include <cstdint>
#include <cstddef>
typedef unsigned int GLuint; typedef int GLint; typedef int GLsizei; typedef unsigned int GLenum;
typedef unsigned char GLboolean; typedef float GLfloat; typedef char GLchar; typedef ptrdiff_t GLsizeiptr; typedef ptrdiff_t GLintptr;
typedef unsigned char GLubyte;
#define APIENTRY
#define GL_TRIANGLES 4
#define GL_UNSIGNED_INT 0x1405
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RED 0x1903
#define GL_R8 0x8229
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_DEPTH_COMPONENT 0x1902
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#define GL_COMPARE_REF_TO_TEXTURE 0x884E
#define GL_LEQUAL 0x0203
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_DEPTH_BUFFER_BIT 0x100
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_CULL_FACE 0x0B44
#define GL_MULTISAMPLE 0x809D
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_ONE 1
#define GL_CLIP_DISTANCE0 0x3000
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_BACK 0x0405
#define GL_NONE 0
void glEnable(GLenum); void glDisable(GLenum); void glClear(unsigned int); void glViewport(int,int,int,int);
void glDepthMask(GLboolean); void glBlendFunc(GLenum,GLenum); void glCullFace(GLenum);
void glGenTextures(GLsizei, GLuint*); void glDeleteTextures(GLsizei, const GLuint*); void glBindTexture(GLenum, GLuint);
void glTexImage2D(GLenum,int,int,int,int,int,GLenum,GLenum,const void*); void glTexParameteri(GLenum,GLenum,int);
void glPixelStorei(GLenum,int); void glDrawArrays(GLenum,int,int); void glDrawElements(GLenum,int,GLenum,const void*);
void glPolygonOffset(float,float); void glDrawBuffer(GLenum); void glReadBuffer(GLenum);
void glReadPixels(int,int,int,int,GLenum,GLenum,void*);
