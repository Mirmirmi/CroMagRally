// Minimal OpenGL 1.x fixed-function to OpenGL ES 3.0 / WebGL2 shim

#ifdef __EMSCRIPTEN__

#define OGL_GLES3_SHIM_IMPL
#include "ogl_gles3_shim.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SHIM_MATRIX_STACK_DEPTH 64
#define SHIM_MAX_LIGHTS 4

typedef struct
{
	GLfloat x, y, z;
	GLfloat u, v;
	GLfloat r, g, b, a;
} ShimImmVertex;

typedef struct
{
	GLboolean   enabled;
	GLint       size;
	GLenum      type;
	GLsizei     stride;
	const void* ptr;
} ShimArrayState;

typedef struct
{
	GLboolean enabled;
	GLfloat   position[4]; // stored in eye space (transformed by modelview at glLightfv time)
	GLfloat   ambient[4];
	GLfloat   diffuse[4];
} ShimLightState;

typedef struct
{
	bool initialized;

	GLuint program;
	GLuint vaVAO;
	GLuint posVBO;
	GLuint normVBO;
	GLuint uvVBO;
	GLuint colVBO;
	GLuint ebo;

	GLuint immVAO;
	GLuint immVBO;

	// Uniform locations
	GLint uModelView;
	GLint uProjection;
	GLint uNormalMat;
	GLint uLighting;
	GLint uNormalize;
	GLint uLightModelAmbient;
	GLint uLightEnabled[SHIM_MAX_LIGHTS];
	GLint uLightPos[SHIM_MAX_LIGHTS];
	GLint uLightAmbient[SHIM_MAX_LIGHTS];
	GLint uLightDiffuse[SHIM_MAX_LIGHTS];
	GLint uTextured;
	GLint uTex;
	GLint uAlphaTestMode;
	GLint uFogEnabled;
	GLint uFogMode;
	GLint uFogStart;
	GLint uFogEnd;
	GLint uFogDensity;
	GLint uFogColor;

	// Matrix stacks
	GLenum  matrixMode;
	GLfloat modelViewStack[SHIM_MATRIX_STACK_DEPTH][16];
	int     modelViewTop;
	GLfloat projectionStack[SHIM_MATRIX_STACK_DEPTH][16];
	int     projectionTop;

	// Fixed-function state
	GLfloat   currentColor[4];
	GLfloat   currentTexCoord[2];
	GLboolean lighting;
	GLboolean normalize;
	GLboolean textured;
	GLboolean alphaTest;
	GLenum    alphaFunc;
	GLfloat   alphaRef;
	GLboolean fog;
	GLenum    fogMode;
	GLfloat   fogStart;
	GLfloat   fogEnd;
	GLfloat   fogDensity;
	GLfloat   fogColor[4];
	GLenum    blendSrc;
	GLenum    blendDst;

	GLfloat        lightModelAmbient[4];
	ShimLightState lights[SHIM_MAX_LIGHTS];

	// Client-side arrays
	ShimArrayState vertexArray;
	ShimArrayState normalArray;
	ShimArrayState texCoordArray;
	ShimArrayState colorArray;

	// Immediate mode accumulation buffer
	GLenum         immMode;
	bool           inBegin;
	ShimImmVertex* immVerts;
	int            immCount;
	int            immCapacity;
	ShimImmVertex* immExpanded;
	int            immExpandedCapacity;

	// Temp buffer for texture pixel format conversion
	uint8_t*       texConvertBuf;
	size_t         texConvertCapacity;
} ShimState;

static ShimState sShim = {
	.matrixMode = GL_MODELVIEW,
	.modelViewStack = {{
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	}},
	.projectionStack = {{
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	}},
	.currentColor = {1.0f, 1.0f, 1.0f, 1.0f},
	.currentTexCoord = {0.0f, 0.0f},
	.alphaFunc = GL_ALWAYS,
	.alphaRef = 0.0f,
	.fogMode = GL_EXP,
	.fogStart = 0.0f,
	.fogEnd = 1.0f,
	.fogDensity = 1.0f,
	.fogColor = {0.0f, 0.0f, 0.0f, 0.0f},
	.blendSrc = GL_ONE,
	.blendDst = GL_ZERO,
	.lightModelAmbient = {0.2f, 0.2f, 0.2f, 1.0f},
};

static const char* kVertexShaderSrc =
	"#version 300 es\n"
	"precision highp float;\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec3 aNormal;\n"
	"layout(location = 2) in vec2 aTexCoord;\n"
	"layout(location = 3) in vec4 aColor;\n"
	"\n"
	"uniform mat4 uModelView;\n"
	"uniform mat4 uProjection;\n"
	"uniform mat3 uNormalMat;\n"
	"uniform int  uLighting;\n"
	"uniform int  uNormalize;\n"
	"uniform vec4 uLightModelAmbient;\n"
	"uniform int  uLightEnabled[4];\n"
	"uniform vec4 uLightPos[4];\n"
	"uniform vec4 uLightAmbient[4];\n"
	"uniform vec4 uLightDiffuse[4];\n"
	"\n"
	"out vec2 vTexCoord;\n"
	"out vec4 vColor;\n"
	"out vec3 vEyePos;\n"
	"\n"
	"void main()\n"
	"{\n"
	"    vec4 eyePos4 = uModelView * vec4(aPos, 1.0);\n"
	"    vEyePos = eyePos4.xyz;\n"
	"    gl_Position = uProjection * eyePos4;\n"
	"    vTexCoord = aTexCoord;\n"
	"    if (uLighting != 0)\n"
	"    {\n"
	"        vec3 N = uNormalMat * aNormal;\n"
	"        if (uNormalize != 0)\n"
	"        {\n"
	"            float len = length(N);\n"
	"            if (len > 1e-6) N /= len;\n"
	"        }\n"
	"        vec3 accum = uLightModelAmbient.rgb * aColor.rgb;\n"
	"        for (int i = 0; i < 4; ++i)\n"
	"        {\n"
	"            if (uLightEnabled[i] != 0)\n"
	"            {\n"
	"                vec3 L = (uLightPos[i].w == 0.0)\n"
	"                    ? normalize(uLightPos[i].xyz)\n"
	"                    : normalize(uLightPos[i].xyz - vEyePos);\n"
	"                float NdotL = max(dot(N, L), 0.0);\n"
	"                accum += aColor.rgb * (uLightAmbient[i].rgb + uLightDiffuse[i].rgb * NdotL);\n"
	"            }\n"
	"        }\n"
	"        vColor = vec4(clamp(accum, 0.0, 1.0), aColor.a);\n"
	"    }\n"
	"    else\n"
	"    {\n"
	"        vColor = aColor;\n"
	"    }\n"
	"}\n";

static const char* kFragmentShaderSrc =
	"#version 300 es\n"
	"precision highp float;\n"
	"in vec2 vTexCoord;\n"
	"in vec4 vColor;\n"
	"in vec3 vEyePos;\n"
	"\n"
	"uniform int       uTextured;\n"
	"uniform sampler2D uTex;\n"
	"uniform int       uAlphaTestMode;\n"
	"uniform int       uFogEnabled;\n"
	"uniform int       uFogMode;\n"
	"uniform float     uFogStart;\n"
	"uniform float     uFogEnd;\n"
	"uniform float     uFogDensity;\n"
	"uniform vec4      uFogColor;\n"
	"\n"
	"out vec4 fragColor;\n"
	"\n"
	"void main()\n"
	"{\n"
	"    vec4 color = vColor;\n"
	"    if (uTextured != 0)\n"
	"    {\n"
	"        color *= texture(uTex, vTexCoord);\n"
	"    }\n"
	"    if (uAlphaTestMode == 1)\n"
	"    {\n"
	"        if (color.a <= (0.5 / 255.0)) discard;\n"
	"    }\n"
	"    else if (uAlphaTestMode == 2)\n"
	"    {\n"
	"        if (color.a < (254.5 / 255.0)) discard;\n"
	"    }\n"
	"    if (uFogEnabled != 0)\n"
	"    {\n"
	"        float dist = length(vEyePos);\n"
	"        float f = 1.0;\n"
	"        if (uFogMode == 0)\n"
	"            f = clamp((uFogEnd - dist) / max(uFogEnd - uFogStart, 0.0001), 0.0, 1.0);\n"
	"        else if (uFogMode == 1)\n"
	"            f = clamp(exp(-uFogDensity * dist), 0.0, 1.0);\n"
	"        else\n"
	"            f = clamp(exp(-uFogDensity * uFogDensity * dist * dist), 0.0, 1.0);\n"
	"        color.rgb = mix(uFogColor.rgb, color.rgb, f);\n"
	"    }\n"
	"    fragColor = color;\n"
	"}\n";

static GLuint CompileShader(GLenum type, const char* src)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);
	GLint ok = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		char logBuf[1024];
		glGetShaderInfoLog(shader, sizeof(logBuf), NULL, logBuf);
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GLES3 shim shader compile error: %s", logBuf);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

static void EnsureInit(void)
{
	if (sShim.initialized)
		return;

	GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexShaderSrc);
	GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShaderSrc);
	GLuint prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glLinkProgram(prog);
	glDeleteShader(vs);
	glDeleteShader(fs);

	GLint linked = 0;
	glGetProgramiv(prog, GL_LINK_STATUS, &linked);
	if (!linked)
	{
		char logBuf[1024];
		glGetProgramInfoLog(prog, sizeof(logBuf), NULL, logBuf);
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GLES3 shim program link error: %s", logBuf);
	}

	sShim.program = prog;
	glUseProgram(prog);

	sShim.uModelView         = glGetUniformLocation(prog, "uModelView");
	sShim.uProjection        = glGetUniformLocation(prog, "uProjection");
	sShim.uNormalMat         = glGetUniformLocation(prog, "uNormalMat");
	sShim.uLighting          = glGetUniformLocation(prog, "uLighting");
	sShim.uNormalize         = glGetUniformLocation(prog, "uNormalize");
	sShim.uLightModelAmbient = glGetUniformLocation(prog, "uLightModelAmbient");
	for (int i = 0; i < SHIM_MAX_LIGHTS; i++)
	{
		char name[32];
		SDL_snprintf(name, sizeof(name), "uLightEnabled[%d]", i);
		sShim.uLightEnabled[i] = glGetUniformLocation(prog, name);
		SDL_snprintf(name, sizeof(name), "uLightPos[%d]", i);
		sShim.uLightPos[i] = glGetUniformLocation(prog, name);
		SDL_snprintf(name, sizeof(name), "uLightAmbient[%d]", i);
		sShim.uLightAmbient[i] = glGetUniformLocation(prog, name);
		SDL_snprintf(name, sizeof(name), "uLightDiffuse[%d]", i);
		sShim.uLightDiffuse[i] = glGetUniformLocation(prog, name);
	}
	sShim.uTextured      = glGetUniformLocation(prog, "uTextured");
	sShim.uTex           = glGetUniformLocation(prog, "uTex");
	sShim.uAlphaTestMode = glGetUniformLocation(prog, "uAlphaTestMode");
	sShim.uFogEnabled    = glGetUniformLocation(prog, "uFogEnabled");
	sShim.uFogMode       = glGetUniformLocation(prog, "uFogMode");
	sShim.uFogStart      = glGetUniformLocation(prog, "uFogStart");
	sShim.uFogEnd        = glGetUniformLocation(prog, "uFogEnd");
	sShim.uFogDensity    = glGetUniformLocation(prog, "uFogDensity");
	sShim.uFogColor      = glGetUniformLocation(prog, "uFogColor");

	glUniform1i(sShim.uTex, 0);

	glGenVertexArrays(1, &sShim.vaVAO);
	glGenBuffers(1, &sShim.posVBO);
	glGenBuffers(1, &sShim.normVBO);
	glGenBuffers(1, &sShim.uvVBO);
	glGenBuffers(1, &sShim.colVBO);
	glGenBuffers(1, &sShim.ebo);

	glGenVertexArrays(1, &sShim.immVAO);
	glGenBuffers(1, &sShim.immVBO);
	glBindVertexArray(sShim.immVAO);
	glBindBuffer(GL_ARRAY_BUFFER, sShim.immVBO);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ShimImmVertex), (const void*)offsetof(ShimImmVertex, x));
	glDisableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ShimImmVertex), (const void*)offsetof(ShimImmVertex, u));
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(ShimImmVertex), (const void*)offsetof(ShimImmVertex, r));
	glBindVertexArray(0);

	sShim.initialized = true;
}

static GLfloat* CurrentMatrix(void)
{
	if (sShim.matrixMode == GL_PROJECTION)
		return sShim.projectionStack[sShim.projectionTop];
	return sShim.modelViewStack[sShim.modelViewTop];
}

static const GLfloat* CurrentModelView(void)
{
	return sShim.modelViewStack[sShim.modelViewTop];
}

static const GLfloat* CurrentProjection(void)
{
	return sShim.projectionStack[sShim.projectionTop];
}

static void Mat4Identity(GLfloat m[16])
{
	SDL_memset(m, 0, sizeof(GLfloat) * 16);
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void Mat4Multiply(GLfloat out[16], const GLfloat a[16], const GLfloat b[16])
{
	GLfloat tmp[16];
	for (int col = 0; col < 4; col++)
	{
		for (int row = 0; row < 4; row++)
		{
			tmp[col * 4 + row] =
				a[0 * 4 + row] * b[col * 4 + 0] +
				a[1 * 4 + row] * b[col * 4 + 1] +
				a[2 * 4 + row] * b[col * 4 + 2] +
				a[3 * 4 + row] * b[col * 4 + 3];
		}
	}
	SDL_memcpy(out, tmp, sizeof(tmp));
}

static void Mat4TransformVec4(GLfloat out[4], const GLfloat m[16], const GLfloat v[4])
{
	GLfloat tmp[4];
	for (int row = 0; row < 4; row++)
	{
		tmp[row] =
			m[0 * 4 + row] * v[0] +
			m[1 * 4 + row] * v[1] +
			m[2 * 4 + row] * v[2] +
			m[3 * 4 + row] * v[3];
	}
	SDL_memcpy(out, tmp, sizeof(tmp));
}

static void ComputeNormalMatrix3x3(GLfloat out9[9], const GLfloat mv[16])
{
	// Column-major 3x3 submatrix A
	float a00 = mv[0], a01 = mv[4], a02 = mv[8];
	float a10 = mv[1], a11 = mv[5], a12 = mv[9];
	float a20 = mv[2], a21 = mv[6], a22 = mv[10];

	float c00 = a11 * a22 - a12 * a21;
	float c01 = -(a10 * a22 - a12 * a20);
	float c02 = a10 * a21 - a11 * a20;

	float c10 = -(a01 * a22 - a02 * a21);
	float c11 = a00 * a22 - a02 * a20;
	float c12 = -(a00 * a21 - a01 * a20);

	float c20 = a01 * a12 - a02 * a11;
	float c21 = -(a00 * a12 - a02 * a10);
	float c22 = a00 * a11 - a01 * a10;

	float det = a00 * c00 + a01 * c01 + a02 * c02;
	if (fabsf(det) < 1e-12f)
	{
		out9[0] = 1; out9[3] = 0; out9[6] = 0;
		out9[1] = 0; out9[4] = 1; out9[7] = 0;
		out9[2] = 0; out9[5] = 0; out9[8] = 1;
		return;
	}

	float invDet = 1.0f / det;
	// transpose(inverse(A)) = cofactor(A) / det, stored column-major
	out9[0] = c00 * invDet; out9[3] = c01 * invDet; out9[6] = c02 * invDet;
	out9[1] = c10 * invDet; out9[4] = c11 * invDet; out9[7] = c12 * invDet;
	out9[2] = c20 * invDet; out9[5] = c21 * invDet; out9[8] = c22 * invDet;
}

static void UploadDrawUniforms(void)
{
	EnsureInit();
	glUseProgram(sShim.program);

	const GLfloat* mv = CurrentModelView();
	const GLfloat* proj = CurrentProjection();
	glUniformMatrix4fv(sShim.uModelView, 1, GL_FALSE, mv);
	glUniformMatrix4fv(sShim.uProjection, 1, GL_FALSE, proj);

	glUniform1i(sShim.uLighting, sShim.lighting ? 1 : 0);
	if (sShim.lighting)
	{
		GLfloat normMat[9];
		ComputeNormalMatrix3x3(normMat, mv);
		glUniformMatrix3fv(sShim.uNormalMat, 1, GL_FALSE, normMat);
		glUniform1i(sShim.uNormalize, sShim.normalize ? 1 : 0);
		glUniform4fv(sShim.uLightModelAmbient, 1, sShim.lightModelAmbient);
		for (int i = 0; i < SHIM_MAX_LIGHTS; i++)
		{
			glUniform1i(sShim.uLightEnabled[i], sShim.lights[i].enabled ? 1 : 0);
			if (sShim.lights[i].enabled)
			{
				glUniform4fv(sShim.uLightPos[i], 1, sShim.lights[i].position);
				glUniform4fv(sShim.uLightAmbient[i], 1, sShim.lights[i].ambient);
				glUniform4fv(sShim.uLightDiffuse[i], 1, sShim.lights[i].diffuse);
			}
		}
	}

	glUniform1i(sShim.uTextured, sShim.textured ? 1 : 0);

	int alphaMode = 0;
	if (sShim.alphaTest)
	{
		if (sShim.alphaFunc == GL_EQUAL && sShim.alphaRef >= 0.99f)
			alphaMode = 2;
		else if (sShim.alphaFunc == GL_NOTEQUAL && sShim.alphaRef <= 0.01f)
			alphaMode = 1;
	}
	glUniform1i(sShim.uAlphaTestMode, alphaMode);

	glUniform1i(sShim.uFogEnabled, sShim.fog ? 1 : 0);
	if (sShim.fog)
	{
		int fMode = 0;
		if (sShim.fogMode == GL_EXP) fMode = 1;
		else if (sShim.fogMode == GL_EXP2) fMode = 2;
		glUniform1i(sShim.uFogMode, fMode);
		glUniform1f(sShim.uFogStart, sShim.fogStart);
		glUniform1f(sShim.uFogEnd, sShim.fogEnd);
		glUniform1f(sShim.uFogDensity, sShim.fogDensity);
		glUniform4fv(sShim.uFogColor, 1, sShim.fogColor);
	}
}

void shim_glEnable(GLenum cap)
{
	switch (cap)
	{
		case GL_LIGHTING:       sShim.lighting = GL_TRUE; break;
		case GL_NORMALIZE:      sShim.normalize = GL_TRUE; break;
		case GL_TEXTURE_2D:     sShim.textured = GL_TRUE; break;
		case GL_ALPHA_TEST:     sShim.alphaTest = GL_TRUE; break;
		case GL_FOG:            sShim.fog = GL_TRUE; break;
		case GL_RESCALE_NORMAL:
		case GL_COLOR_MATERIAL: break;
		default:
			if (cap >= GL_LIGHT0 && cap < GL_LIGHT0 + SHIM_MAX_LIGHTS)
				sShim.lights[cap - GL_LIGHT0].enabled = GL_TRUE;
			else
				glEnable(cap);
			break;
	}
}

void shim_glDisable(GLenum cap)
{
	switch (cap)
	{
		case GL_LIGHTING:       sShim.lighting = GL_FALSE; break;
		case GL_NORMALIZE:      sShim.normalize = GL_FALSE; break;
		case GL_TEXTURE_2D:     sShim.textured = GL_FALSE; break;
		case GL_ALPHA_TEST:     sShim.alphaTest = GL_FALSE; break;
		case GL_FOG:            sShim.fog = GL_FALSE; break;
		case GL_RESCALE_NORMAL:
		case GL_COLOR_MATERIAL: break;
		default:
			if (cap >= GL_LIGHT0 && cap < GL_LIGHT0 + SHIM_MAX_LIGHTS)
				sShim.lights[cap - GL_LIGHT0].enabled = GL_FALSE;
			else
				glDisable(cap);
			break;
	}
}

GLboolean shim_glIsEnabled(GLenum cap)
{
	switch (cap)
	{
		case GL_LIGHTING:       return sShim.lighting;
		case GL_NORMALIZE:      return sShim.normalize;
		case GL_TEXTURE_2D:     return sShim.textured;
		case GL_ALPHA_TEST:     return sShim.alphaTest;
		case GL_FOG:            return sShim.fog;
		case GL_COLOR_MATERIAL: return GL_TRUE;
		case GL_RESCALE_NORMAL: return GL_FALSE;
		default:
			if (cap >= GL_LIGHT0 && cap < GL_LIGHT0 + SHIM_MAX_LIGHTS)
				return sShim.lights[cap - GL_LIGHT0].enabled;
			return glIsEnabled(cap);
	}
}

void shim_glGetIntegerv(GLenum pname, GLint* data)
{
	if (!data)
		return;
	switch (pname)
	{
		case GL_BLEND_SRC: *data = (GLint)sShim.blendSrc; break;
		case GL_BLEND_DST: *data = (GLint)sShim.blendDst; break;
		default:           glGetIntegerv(pname, data); break;
	}
}

void shim_glGetFloatv(GLenum pname, GLfloat* data)
{
	if (!data)
		return;
	if (pname == GL_CURRENT_COLOR)
	{
		SDL_memcpy(data, sShim.currentColor, sizeof(GLfloat) * 4);
		return;
	}
	glGetFloatv(pname, data);
}

void shim_glBlendFunc(GLenum sfactor, GLenum dfactor)
{
	sShim.blendSrc = sfactor;
	sShim.blendDst = dfactor;
	glBlendFunc(sfactor, dfactor);
}

void shim_glTexImage2D(GLenum target, GLint level, GLint internalformat,
                       GLsizei width, GLsizei height, GLint border,
                       GLenum format, GLenum type, const void* pixels)
{
	if (pixels && format == GL_BGRA && type == GL_UNSIGNED_SHORT_1_5_5_5_REV)
	{
		size_t numPixels = (size_t)width * (size_t)height;
		size_t needed = numPixels * 4;
		if (needed > sShim.texConvertCapacity)
		{
			uint8_t* grown = (uint8_t*)SDL_realloc(sShim.texConvertBuf, needed);
			if (!grown)
				return;
			sShim.texConvertBuf = grown;
			sShim.texConvertCapacity = needed;
		}
		const uint16_t* src = (const uint16_t*)pixels;
		uint8_t* dst = sShim.texConvertBuf;
		bool forceOpaque = (internalformat == GL_RGB);
		for (size_t i = 0; i < numPixels; i++)
		{
			uint16_t p = src[i];
			uint8_t b5 = (uint8_t)(p & 0x1F);
			uint8_t g5 = (uint8_t)((p >> 5) & 0x1F);
			uint8_t r5 = (uint8_t)((p >> 10) & 0x1F);
			uint8_t a1 = (uint8_t)((p >> 15) & 0x01);
			dst[i * 4 + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
			dst[i * 4 + 1] = (uint8_t)((g5 << 3) | (g5 >> 2));
			dst[i * 4 + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
			dst[i * 4 + 3] = forceOpaque ? 255 : (a1 ? 255 : 0);
		}
		glTexImage2D(target, level, GL_RGBA, width, height, border, GL_RGBA, GL_UNSIGNED_BYTE, dst);
		return;
	}

	if (pixels && format == GL_BGRA && type == GL_UNSIGNED_BYTE)
	{
		size_t numPixels = (size_t)width * (size_t)height;
		size_t needed = numPixels * 4;
		if (needed > sShim.texConvertCapacity)
		{
			uint8_t* grown = (uint8_t*)SDL_realloc(sShim.texConvertBuf, needed);
			if (!grown)
				return;
			sShim.texConvertBuf = grown;
			sShim.texConvertCapacity = needed;
		}
		const uint8_t* src = (const uint8_t*)pixels;
		uint8_t* dst = sShim.texConvertBuf;
		for (size_t i = 0; i < numPixels; i++)
		{
			dst[i * 4 + 0] = src[i * 4 + 2];
			dst[i * 4 + 1] = src[i * 4 + 1];
			dst[i * 4 + 2] = src[i * 4 + 0];
			dst[i * 4 + 3] = src[i * 4 + 3];
		}
		GLint resolvedInternal = (internalformat == GL_RGB) ? GL_RGB : GL_RGBA;
		glTexImage2D(target, level, resolvedInternal, width, height, border, GL_RGBA, GL_UNSIGNED_BYTE, dst);
		return;
	}

	GLint resolvedInternal = internalformat;
	if (resolvedInternal == GL_RGB5_A1)
		resolvedInternal = GL_RGBA;
	if (format == GL_RGBA && resolvedInternal == GL_RGB)
		resolvedInternal = GL_RGBA;
	else if (format == GL_RGB && resolvedInternal == GL_RGBA)
		resolvedInternal = GL_RGB;

	glTexImage2D(target, level, resolvedInternal, width, height, border, format, type, pixels);
}

void shim_glMatrixMode(GLenum mode)
{
	sShim.matrixMode = mode;
}

void shim_glLoadIdentity(void)
{
	Mat4Identity(CurrentMatrix());
}

void shim_glPushMatrix(void)
{
	if (sShim.matrixMode == GL_PROJECTION)
	{
		if (sShim.projectionTop + 1 < SHIM_MATRIX_STACK_DEPTH)
		{
			SDL_memcpy(sShim.projectionStack[sShim.projectionTop + 1],
			           sShim.projectionStack[sShim.projectionTop], sizeof(GLfloat) * 16);
			sShim.projectionTop++;
		}
	}
	else
	{
		if (sShim.modelViewTop + 1 < SHIM_MATRIX_STACK_DEPTH)
		{
			SDL_memcpy(sShim.modelViewStack[sShim.modelViewTop + 1],
			           sShim.modelViewStack[sShim.modelViewTop], sizeof(GLfloat) * 16);
			sShim.modelViewTop++;
		}
	}
}

void shim_glPopMatrix(void)
{
	if (sShim.matrixMode == GL_PROJECTION)
	{
		if (sShim.projectionTop > 0)
			sShim.projectionTop--;
	}
	else
	{
		if (sShim.modelViewTop > 0)
			sShim.modelViewTop--;
	}
}

void shim_glLoadMatrixf(const GLfloat* m)
{
	if (m)
		SDL_memcpy(CurrentMatrix(), m, sizeof(GLfloat) * 16);
}

void shim_glMultMatrixf(const GLfloat* m)
{
	if (m)
	{
		GLfloat* cur = CurrentMatrix();
		Mat4Multiply(cur, cur, m);
	}
}

void shim_glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble nearVal, GLdouble farVal)
{
	GLfloat m[16];
	Mat4Identity(m);
	GLdouble rl = right - left;
	GLdouble tb = top - bottom;
	GLdouble fn = farVal - nearVal;
	m[0]  = (GLfloat)(2.0 / rl);
	m[5]  = (GLfloat)(2.0 / tb);
	m[10] = (GLfloat)(-2.0 / fn);
	m[12] = (GLfloat)(-(right + left) / rl);
	m[13] = (GLfloat)(-(top + bottom) / tb);
	m[14] = (GLfloat)(-(farVal + nearVal) / fn);
	GLfloat* cur = CurrentMatrix();
	Mat4Multiply(cur, cur, m);
}

void shim_glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
	GLfloat m[16];
	Mat4Identity(m);
	m[12] = x;
	m[13] = y;
	m[14] = z;
	GLfloat* cur = CurrentMatrix();
	Mat4Multiply(cur, cur, m);
}

void shim_glScalef(GLfloat x, GLfloat y, GLfloat z)
{
	GLfloat m[16];
	Mat4Identity(m);
	m[0]  = x;
	m[5]  = y;
	m[10] = z;
	GLfloat* cur = CurrentMatrix();
	Mat4Multiply(cur, cur, m);
}

void shim_glRotatef(GLfloat angleDeg, GLfloat x, GLfloat y, GLfloat z)
{
	float len = sqrtf(x * x + y * y + z * z);
	if (len < 1e-6f)
		return;
	x /= len;
	y /= len;
	z /= len;

	float rad = angleDeg * (3.14159265358979323846f / 180.0f);
	float c = cosf(rad);
	float s = sinf(rad);
	float t = 1.0f - c;

	GLfloat m[16];
	Mat4Identity(m);
	m[0]  = t * x * x + c;
	m[1]  = t * x * y + s * z;
	m[2]  = t * x * z - s * y;

	m[4]  = t * x * y - s * z;
	m[5]  = t * y * y + c;
	m[6]  = t * y * z + s * x;

	m[8]  = t * x * z + s * y;
	m[9]  = t * y * z - s * x;
	m[10] = t * z * z + c;

	GLfloat* cur = CurrentMatrix();
	Mat4Multiply(cur, cur, m);
}

void shim_glColor3f(GLfloat red, GLfloat green, GLfloat blue)
{
	sShim.currentColor[0] = red;
	sShim.currentColor[1] = green;
	sShim.currentColor[2] = blue;
	sShim.currentColor[3] = 1.0f;
}

void shim_glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
	sShim.currentColor[0] = red;
	sShim.currentColor[1] = green;
	sShim.currentColor[2] = blue;
	sShim.currentColor[3] = alpha;
}

void shim_glColor4fv(const GLfloat* v)
{
	if (v)
		SDL_memcpy(sShim.currentColor, v, sizeof(GLfloat) * 4);
}

void shim_glTexCoord2f(GLfloat s, GLfloat t)
{
	sShim.currentTexCoord[0] = s;
	sShim.currentTexCoord[1] = t;
}

void shim_glBegin(GLenum mode)
{
	sShim.immMode = mode;
	sShim.immCount = 0;
	sShim.inBegin = true;
}

static void PushImmVertex(GLfloat x, GLfloat y, GLfloat z)
{
	if (!sShim.inBegin)
		return;
	if (sShim.immCount >= sShim.immCapacity)
	{
		int newCap = sShim.immCapacity ? sShim.immCapacity * 2 : 1024;
		ShimImmVertex* grown = (ShimImmVertex*)SDL_realloc(sShim.immVerts, (size_t)newCap * sizeof(ShimImmVertex));
		if (!grown)
			return;
		sShim.immVerts = grown;
		sShim.immCapacity = newCap;
	}
	ShimImmVertex* v = &sShim.immVerts[sShim.immCount++];
	v->x = x;
	v->y = y;
	v->z = z;
	v->u = sShim.currentTexCoord[0];
	v->v = sShim.currentTexCoord[1];
	v->r = sShim.currentColor[0];
	v->g = sShim.currentColor[1];
	v->b = sShim.currentColor[2];
	v->a = sShim.currentColor[3];
}

void shim_glVertex2f(GLfloat x, GLfloat y)
{
	PushImmVertex(x, y, 0.0f);
}

void shim_glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
	PushImmVertex(x, y, z);
}

void shim_glEnd(void)
{
	if (!sShim.inBegin)
		return;
	sShim.inBegin = false;

	if (sShim.immCount <= 0)
		return;

	const ShimImmVertex* drawVerts = sShim.immVerts;
	int drawCount = sShim.immCount;
	GLenum drawMode = sShim.immMode;

	if (sShim.immMode == GL_QUADS)
	{
		int numQuads = sShim.immCount / 4;
		if (numQuads <= 0)
			return;
		int expandedCount = numQuads * 6;
		if (expandedCount > sShim.immExpandedCapacity)
		{
			int newCap = expandedCount + 256;
			ShimImmVertex* grown = (ShimImmVertex*)SDL_realloc(sShim.immExpanded, (size_t)newCap * sizeof(ShimImmVertex));
			if (!grown)
				return;
			sShim.immExpanded = grown;
			sShim.immExpandedCapacity = newCap;
		}
		for (int q = 0; q < numQuads; q++)
		{
			const ShimImmVertex* src = &sShim.immVerts[q * 4];
			ShimImmVertex* dst = &sShim.immExpanded[q * 6];
			dst[0] = src[0];
			dst[1] = src[1];
			dst[2] = src[2];
			dst[3] = src[0];
			dst[4] = src[2];
			dst[5] = src[3];
		}
		drawVerts = sShim.immExpanded;
		drawCount = expandedCount;
		drawMode = GL_TRIANGLES;
	}

	UploadDrawUniforms();

	glBindVertexArray(sShim.immVAO);
	glVertexAttrib3f(1, 0.0f, 1.0f, 0.0f);
	glBindBuffer(GL_ARRAY_BUFFER, sShim.immVBO);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)drawCount * sizeof(ShimImmVertex)), drawVerts, GL_STREAM_DRAW);
	glDrawArrays(drawMode, 0, drawCount);
	glBindVertexArray(0);
}

void shim_glEnableClientState(GLenum array)
{
	switch (array)
	{
		case GL_VERTEX_ARRAY:        sShim.vertexArray.enabled = GL_TRUE; break;
		case GL_NORMAL_ARRAY:        sShim.normalArray.enabled = GL_TRUE; break;
		case GL_TEXTURE_COORD_ARRAY: sShim.texCoordArray.enabled = GL_TRUE; break;
		case GL_COLOR_ARRAY:         sShim.colorArray.enabled = GL_TRUE; break;
		default: break;
	}
}

void shim_glDisableClientState(GLenum array)
{
	switch (array)
	{
		case GL_VERTEX_ARRAY:        sShim.vertexArray.enabled = GL_FALSE; break;
		case GL_NORMAL_ARRAY:        sShim.normalArray.enabled = GL_FALSE; break;
		case GL_TEXTURE_COORD_ARRAY: sShim.texCoordArray.enabled = GL_FALSE; break;
		case GL_COLOR_ARRAY:         sShim.colorArray.enabled = GL_FALSE; break;
		default: break;
	}
}

void shim_glVertexPointer(GLint size, GLenum type, GLsizei stride, const void* pointer)
{
	sShim.vertexArray.size = size;
	sShim.vertexArray.type = type;
	sShim.vertexArray.stride = stride;
	sShim.vertexArray.ptr = pointer;
}

void shim_glNormalPointer(GLenum type, GLsizei stride, const void* pointer)
{
	sShim.normalArray.size = 3;
	sShim.normalArray.type = type;
	sShim.normalArray.stride = stride;
	sShim.normalArray.ptr = pointer;
}

void shim_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void* pointer)
{
	sShim.texCoordArray.size = size;
	sShim.texCoordArray.type = type;
	sShim.texCoordArray.stride = stride;
	sShim.texCoordArray.ptr = pointer;
}

void shim_glColorPointer(GLint size, GLenum type, GLsizei stride, const void* pointer)
{
	sShim.colorArray.size = size;
	sShim.colorArray.type = type;
	sShim.colorArray.stride = stride;
	sShim.colorArray.ptr = pointer;
}

static size_t TypeSize(GLenum type)
{
	switch (type)
	{
		case GL_BYTE:
		case GL_UNSIGNED_BYTE:  return 1;
		case GL_SHORT:
		case GL_UNSIGNED_SHORT: return 2;
		case GL_INT:
		case GL_UNSIGNED_INT:
		case GL_FLOAT:          return 4;
		default:                return 4;
	}
}

void shim_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices)
{
	if (!sShim.vertexArray.enabled || !sShim.vertexArray.ptr || !indices || count <= 0)
		return;

	uint32_t maxIndex = 0;
	if (type == GL_UNSIGNED_INT)
	{
		const uint32_t* idx = (const uint32_t*)indices;
		for (GLsizei i = 0; i < count; i++)
		{
			if (idx[i] > maxIndex)
				maxIndex = idx[i];
		}
	}
	else if (type == GL_UNSIGNED_SHORT)
	{
		const uint16_t* idx = (const uint16_t*)indices;
		for (GLsizei i = 0; i < count; i++)
		{
			if (idx[i] > maxIndex)
				maxIndex = idx[i];
		}
	}
	else
	{
		return;
	}

	size_t numVerts = (size_t)maxIndex + 1;

	UploadDrawUniforms();

	glBindVertexArray(sShim.vaVAO);

	// 0: Position
	{
		size_t strideBytes = sShim.vertexArray.stride
			? (size_t)sShim.vertexArray.stride
			: (size_t)sShim.vertexArray.size * TypeSize(sShim.vertexArray.type);
		glBindBuffer(GL_ARRAY_BUFFER, sShim.posVBO);
		glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(numVerts * strideBytes), sShim.vertexArray.ptr, GL_STREAM_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, sShim.vertexArray.size, sShim.vertexArray.type, GL_FALSE, sShim.vertexArray.stride, (const void*)0);
	}

	// 1: Normal
	if (sShim.normalArray.enabled && sShim.normalArray.ptr)
	{
		size_t strideBytes = sShim.normalArray.stride
			? (size_t)sShim.normalArray.stride
			: 3 * TypeSize(sShim.normalArray.type);
		glBindBuffer(GL_ARRAY_BUFFER, sShim.normVBO);
		glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(numVerts * strideBytes), sShim.normalArray.ptr, GL_STREAM_DRAW);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, sShim.normalArray.type, GL_FALSE, sShim.normalArray.stride, (const void*)0);
	}
	else
	{
		glDisableVertexAttribArray(1);
		glVertexAttrib3f(1, 0.0f, 1.0f, 0.0f);
	}

	// 2: TexCoord
	if (sShim.texCoordArray.enabled && sShim.texCoordArray.ptr)
	{
		size_t strideBytes = sShim.texCoordArray.stride
			? (size_t)sShim.texCoordArray.stride
			: (size_t)sShim.texCoordArray.size * TypeSize(sShim.texCoordArray.type);
		glBindBuffer(GL_ARRAY_BUFFER, sShim.uvVBO);
		glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(numVerts * strideBytes), sShim.texCoordArray.ptr, GL_STREAM_DRAW);
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, sShim.texCoordArray.size, sShim.texCoordArray.type, GL_FALSE, sShim.texCoordArray.stride, (const void*)0);
	}
	else
	{
		glDisableVertexAttribArray(2);
		glVertexAttrib2f(2, sShim.currentTexCoord[0], sShim.currentTexCoord[1]);
	}

	// 3: Color
	if (sShim.colorArray.enabled && sShim.colorArray.ptr)
	{
		size_t strideBytes = sShim.colorArray.stride
			? (size_t)sShim.colorArray.stride
			: (size_t)sShim.colorArray.size * TypeSize(sShim.colorArray.type);
		GLboolean normalized = (sShim.colorArray.type == GL_UNSIGNED_BYTE) ? GL_TRUE : GL_FALSE;
		glBindBuffer(GL_ARRAY_BUFFER, sShim.colVBO);
		glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(numVerts * strideBytes), sShim.colorArray.ptr, GL_STREAM_DRAW);
		glEnableVertexAttribArray(3);
		glVertexAttribPointer(3, sShim.colorArray.size, sShim.colorArray.type, normalized, sShim.colorArray.stride, (const void*)0);
	}
	else
	{
		glDisableVertexAttribArray(3);
		glVertexAttrib4fv(3, sShim.currentColor);
	}

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sShim.ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)count * TypeSize(type)), indices, GL_STREAM_DRAW);
	glDrawElements(mode, count, type, (const void*)0);
	glBindVertexArray(0);
}

void shim_glAlphaFunc(GLenum func, GLclampf ref)
{
	sShim.alphaFunc = func;
	sShim.alphaRef = ref;
}

void shim_glFogi(GLenum pname, GLint param)
{
	if (pname == GL_FOG_MODE)
		sShim.fogMode = (GLenum)param;
}

void shim_glFogf(GLenum pname, GLfloat param)
{
	switch (pname)
	{
		case GL_FOG_MODE:    sShim.fogMode = (GLenum)param; break;
		case GL_FOG_DENSITY: sShim.fogDensity = param; break;
		case GL_FOG_START:   sShim.fogStart = param; break;
		case GL_FOG_END:     sShim.fogEnd = param; break;
		default: break;
	}
}

void shim_glFogfv(GLenum pname, const GLfloat* params)
{
	if (pname == GL_FOG_COLOR && params)
		SDL_memcpy(sShim.fogColor, params, sizeof(GLfloat) * 4);
}

void shim_glLightModelfv(GLenum pname, const GLfloat* params)
{
	if (pname == GL_LIGHT_MODEL_AMBIENT && params)
		SDL_memcpy(sShim.lightModelAmbient, params, sizeof(GLfloat) * 4);
}

void shim_glLightfv(GLenum light, GLenum pname, const GLfloat* params)
{
	if (!params || light < GL_LIGHT0 || light >= GL_LIGHT0 + SHIM_MAX_LIGHTS)
		return;
	ShimLightState* l = &sShim.lights[light - GL_LIGHT0];
	switch (pname)
	{
		case GL_POSITION:
			Mat4TransformVec4(l->position, CurrentModelView(), params);
			break;
		case GL_AMBIENT:
			SDL_memcpy(l->ambient, params, sizeof(GLfloat) * 4);
			break;
		case GL_DIFFUSE:
			SDL_memcpy(l->diffuse, params, sizeof(GLfloat) * 4);
			break;
		default:
			break;
	}
}

void shim_glMaterialfv(GLenum face, GLenum pname, const GLfloat* params)
{
	(void)face;
	(void)pname;
	(void)params;
}

void shim_glColorMaterial(GLenum face, GLenum mode)
{
	(void)face;
	(void)mode;
}

void shim_glHint(GLenum target, GLenum mode)
{
	(void)target;
	(void)mode;
}

void shim_glPolygonMode(GLenum face, GLenum mode)
{
	(void)face;
	(void)mode;
}

void shim_glShutdown(void)
{
	if (sShim.initialized)
	{
		glDeleteVertexArrays(1, &sShim.vaVAO);
		glDeleteBuffers(1, &sShim.posVBO);
		glDeleteBuffers(1, &sShim.normVBO);
		glDeleteBuffers(1, &sShim.uvVBO);
		glDeleteBuffers(1, &sShim.colVBO);
		glDeleteBuffers(1, &sShim.ebo);
		glDeleteVertexArrays(1, &sShim.immVAO);
		glDeleteBuffers(1, &sShim.immVBO);
		glDeleteProgram(sShim.program);
		sShim.initialized = false;
	}
	SDL_free(sShim.immVerts);
	sShim.immVerts = NULL;
	sShim.immCapacity = 0;
	SDL_free(sShim.immExpanded);
	sShim.immExpanded = NULL;
	sShim.immExpandedCapacity = 0;
	SDL_free(sShim.texConvertBuf);
	sShim.texConvertBuf = NULL;
	sShim.texConvertCapacity = 0;
}

#endif // __EMSCRIPTEN__
