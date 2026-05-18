/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS compositor state and diagnostics
 */

#include "ohos_compositor.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <winpr/crt.h>
#include <winpr/synch.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <native_image/native_image.h>
#include <native_window/external_window.h>

#define OHOS_COMPOSITOR_DIAGNOSTICS_SIZE 1536

#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

struct freerdp_ohos_compositor
{
	CRITICAL_SECTION lock;
	BOOL initialized;
	FREERDP_OHOS_COMPOSITOR_LOG_CALLBACK log;
	void* userData;
	void* outputWindow;
	UINT32 outputWidth;
	UINT32 outputHeight;
	UINT32 outputGeneration;
	UINT32 mode;
	UINT64 outputTargetSets;
	UINT64 outputTargetClears;
	UINT64 avc420Begins;
	UINT64 avc420Ends;
	UINT64 avc444SurfaceSets;
	UINT64 avc444SurfaceClears;
	UINT64 avc444Frames;
	UINT64 avc444RenderAttempts;
	UINT64 avc444RenderSuccess;
	UINT64 avc444RenderFailures;
	BOOL avc444SurfacesReady;
	UINT32 avc444Width;
	UINT32 avc444Height;
	void* avc444LumaImage;
	void* avc444ChromaImage;
	void* avc444EglDisplay;
	void* avc444EglConfig;
	void* avc444EglContext;
	EGLSurface outputEglSurface;
	UINT32 outputEglGeneration;
	GLuint avc444Program;
	GLuint avc444Vbo;
	GLint avc444PositionAttrib;
	GLint avc444TexCoordAttrib;
	GLint avc444LumaUniform;
	GLint avc444ChromaUniform;
	GLint avc444OpUniform;
	GLint avc444CodecUniform;
	UINT32 avc444LumaTexture;
	UINT32 avc444ChromaTexture;
	UINT64 avc444LumaSurfaceId;
	UINT64 avc444ChromaSurfaceId;
	UINT32 lastAvc444SurfaceId;
	UINT32 lastAvc444Width;
	UINT32 lastAvc444Height;
	UINT32 lastAvc444Op;
	UINT32 lastAvc444CodecId;
	char lastRenderError[160];
	char diagnostics[OHOS_COMPOSITOR_DIAGNOSTICS_SIZE];
};

static void ohos_compositor_format_message(char* message, size_t size, const char* format, ...)
{
	if (!message || size == 0)
		return;

	va_list args;
	va_start(args, format);
	(void)vsnprintf(message, size, format, args);
	va_end(args);
}

static const char* ohos_compositor_mode_name(UINT32 mode)
{
	switch (mode)
	{
		case FREERDP_OHOS_COMPOSITOR_MODE_RGBA:
			return "rgba";
		case FREERDP_OHOS_COMPOSITOR_MODE_AVC420_SURFACE:
			return "avc420-surface";
		case FREERDP_OHOS_COMPOSITOR_MODE_AVC444_GPU:
			return "avc444-gpu";
		case FREERDP_OHOS_COMPOSITOR_MODE_NONE:
		default:
			return "none";
	}
}

static void ohos_compositor_log(freerdpOhosCompositor* compositor, const char* format, ...)
{
	if (!compositor)
		return;

	FREERDP_OHOS_COMPOSITOR_LOG_CALLBACK log = NULL;
	void* userData = NULL;
	EnterCriticalSection(&compositor->lock);
	log = compositor->log;
	userData = compositor->userData;
	LeaveCriticalSection(&compositor->lock);
	if (!log)
		return;

	char message[384] = { 0 };
	va_list args;
	va_start(args, format);
	(void)vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	log(message, userData);
}

static void ohos_compositor_set_render_error_locked(freerdpOhosCompositor* compositor,
                                                    const char* format, ...)
{
	if (!compositor)
		return;

	va_list args;
	va_start(args, format);
	(void)vsnprintf(compositor->lastRenderError, sizeof(compositor->lastRenderError), format,
	                args);
	va_end(args);
}

static void ohos_compositor_destroy_gl_locked(freerdpOhosCompositor* compositor)
{
	if (!compositor)
		return;

	const EGLDisplay display = (EGLDisplay)compositor->avc444EglDisplay;
	const EGLContext context = (EGLContext)compositor->avc444EglContext;
	if ((display != EGL_NO_DISPLAY) && (context != EGL_NO_CONTEXT) &&
	    (compositor->outputEglSurface != EGL_NO_SURFACE))
	{
		(void)eglMakeCurrent(display, compositor->outputEglSurface,
		                     compositor->outputEglSurface, context);
		if (compositor->avc444Program != 0)
			glDeleteProgram(compositor->avc444Program);
		if (compositor->avc444Vbo != 0)
			glDeleteBuffers(1, &compositor->avc444Vbo);
		(void)eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		(void)eglDestroySurface(display, compositor->outputEglSurface);
	}

	compositor->outputEglSurface = EGL_NO_SURFACE;
	compositor->outputEglGeneration = 0;
	compositor->avc444Program = 0;
	compositor->avc444Vbo = 0;
	compositor->avc444PositionAttrib = -1;
	compositor->avc444TexCoordAttrib = -1;
	compositor->avc444LumaUniform = -1;
	compositor->avc444ChromaUniform = -1;
	compositor->avc444OpUniform = -1;
	compositor->avc444CodecUniform = -1;
}

static BOOL ohos_compositor_shader_status(GLuint shader, char* message, size_t messageSize)
{
	GLint ok = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (ok == GL_TRUE)
		return TRUE;

	char log[256] = { 0 };
	glGetShaderInfoLog(shader, sizeof(log), NULL, log);
	ohos_compositor_format_message(message, messageSize, "%s",
	                               log[0] ? log : "shader compile failed");
	return FALSE;
}

static GLuint ohos_compositor_compile_shader(GLenum type, const char* source, char* message,
                                             size_t messageSize)
{
	GLuint shader = glCreateShader(type);
	if (shader == 0)
	{
		ohos_compositor_format_message(message, messageSize,
		                               "create shader failed: gl=0x%08" PRIX32,
		                               (UINT32)glGetError());
		return 0;
	}

	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	if (!ohos_compositor_shader_status(shader, message, messageSize))
	{
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

static BOOL ohos_compositor_ensure_program_locked(freerdpOhosCompositor* compositor,
                                                  char* message, size_t messageSize)
{
	if (!compositor)
		return FALSE;
	if (compositor->avc444Program != 0)
		return TRUE;

	static const char* vertexShader =
	    "#version 300 es\n"
	    "layout(location = 0) in vec2 aPosition;\n"
	    "layout(location = 1) in vec2 aTexCoord;\n"
	    "out vec2 vTexCoord;\n"
	    "void main() {\n"
	    "  gl_Position = vec4(aPosition, 0.0, 1.0);\n"
	    "  vTexCoord = aTexCoord;\n"
	    "}\n";
	static const char* fragmentShader =
	    "#version 300 es\n"
	    "#extension GL_OES_EGL_image_external_essl3 : require\n"
	    "precision mediump float;\n"
	    "in vec2 vTexCoord;\n"
	    "uniform samplerExternalOES uLuma;\n"
	    "uniform samplerExternalOES uChroma;\n"
	    "uniform int uOp;\n"
	    "uniform int uCodec;\n"
	    "out vec4 fragColor;\n"
	    "vec3 rgbToYuv(vec3 c) {\n"
	    "  float y = dot(c, vec3(0.299, 0.587, 0.114));\n"
	    "  float u = (c.b - y) * 0.565 + 0.5;\n"
	    "  float v = (c.r - y) * 0.713 + 0.5;\n"
	    "  return vec3(y, u, v);\n"
	    "}\n"
	    "vec3 yuvToRgb(float y, float u, float v) {\n"
	    "  u -= 0.5;\n"
	    "  v -= 0.5;\n"
	    "  return clamp(vec3(y + 1.403 * v, y - 0.344 * u - 0.714 * v, y + 1.770 * u), 0.0, 1.0);\n"
	    "}\n"
	    "void main() {\n"
	    "  vec3 l = rgbToYuv(texture(uLuma, vTexCoord).rgb);\n"
	    "  vec3 c = rgbToYuv(texture(uChroma, vTexCoord).rgb);\n"
	    "  float y = l.x;\n"
	    "  float u = c.y;\n"
	    "  float v = c.z;\n"
	    "  fragColor = vec4(yuvToRgb(y, u, v), 1.0);\n"
	    "}\n";

	char shaderMessage[256] = { 0 };
	GLuint vs = ohos_compositor_compile_shader(GL_VERTEX_SHADER, vertexShader, shaderMessage,
	                                           sizeof(shaderMessage));
	if (vs == 0)
	{
		ohos_compositor_format_message(message, messageSize, "vertex %s", shaderMessage);
		return FALSE;
	}
	GLuint fs = ohos_compositor_compile_shader(GL_FRAGMENT_SHADER, fragmentShader, shaderMessage,
	                                           sizeof(shaderMessage));
	if (fs == 0)
	{
		glDeleteShader(vs);
		ohos_compositor_format_message(message, messageSize, "fragment %s", shaderMessage);
		return FALSE;
	}

	GLuint program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	glDeleteShader(vs);
	glDeleteShader(fs);

	GLint linked = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (linked != GL_TRUE)
	{
		char log[256] = { 0 };
		glGetProgramInfoLog(program, sizeof(log), NULL, log);
		glDeleteProgram(program);
		ohos_compositor_format_message(message, messageSize, "program link failed: %s",
		                               log[0] ? log : "unknown");
		return FALSE;
	}

	compositor->avc444Program = program;
	compositor->avc444PositionAttrib = 0;
	compositor->avc444TexCoordAttrib = 1;
	compositor->avc444LumaUniform = glGetUniformLocation(program, "uLuma");
	compositor->avc444ChromaUniform = glGetUniformLocation(program, "uChroma");
	compositor->avc444OpUniform = glGetUniformLocation(program, "uOp");
	compositor->avc444CodecUniform = glGetUniformLocation(program, "uCodec");
	if ((compositor->avc444LumaUniform < 0) || (compositor->avc444ChromaUniform < 0))
	{
		glDeleteProgram(program);
		compositor->avc444Program = 0;
		ohos_compositor_format_message(message, messageSize,
		                               "program uniforms unavailable");
		return FALSE;
	}
	return TRUE;
}

static BOOL ohos_compositor_ensure_output_surface_locked(freerdpOhosCompositor* compositor,
                                                         char* message, size_t messageSize)
{
	if (!compositor)
		return FALSE;

	const EGLDisplay display = (EGLDisplay)compositor->avc444EglDisplay;
	const EGLConfig config = (EGLConfig)compositor->avc444EglConfig;
	if ((display == EGL_NO_DISPLAY) || !config || !compositor->outputWindow)
	{
		ohos_compositor_format_message(message, messageSize,
		                               "output EGL target is unavailable");
		return FALSE;
	}

	if ((compositor->outputEglSurface != EGL_NO_SURFACE) &&
	    (compositor->outputEglGeneration == compositor->outputGeneration))
		return TRUE;

	ohos_compositor_destroy_gl_locked(compositor);
	compositor->outputEglSurface = eglCreateWindowSurface(
	    display, config, (EGLNativeWindowType)compositor->outputWindow, NULL);
	if (compositor->outputEglSurface == EGL_NO_SURFACE)
	{
		ohos_compositor_format_message(message, messageSize,
		                               "create output EGL surface failed: egl=0x%08" PRIX32,
		                               (UINT32)eglGetError());
		return FALSE;
	}
	compositor->outputEglGeneration = compositor->outputGeneration;
	return TRUE;
}

static BOOL ohos_compositor_render_avc444_locked(freerdpOhosCompositor* compositor,
                                                 UINT32 op, UINT32 codecId, char* message,
                                                 size_t messageSize)
{
	if (!compositor || !compositor->avc444SurfacesReady || !compositor->outputWindow)
	{
		ohos_compositor_format_message(message, messageSize,
		                               "AVC444 compositor target is unavailable");
		return FALSE;
	}

	const EGLDisplay display = (EGLDisplay)compositor->avc444EglDisplay;
	const EGLContext context = (EGLContext)compositor->avc444EglContext;
	if ((display == EGL_NO_DISPLAY) || (context == EGL_NO_CONTEXT))
	{
		ohos_compositor_format_message(message, messageSize,
		                               "AVC444 compositor EGL context is unavailable");
		return FALSE;
	}
	if (!ohos_compositor_ensure_output_surface_locked(compositor, message, messageSize))
		return FALSE;
	if (!eglMakeCurrent(display, compositor->outputEglSurface, compositor->outputEglSurface,
	                    context))
	{
		ohos_compositor_format_message(message, messageSize,
		                               "make AVC444 compositor current failed: egl=0x%08" PRIX32,
		                               (UINT32)eglGetError());
		return FALSE;
	}

	if (OH_NativeImage_UpdateSurfaceImage((OH_NativeImage*)compositor->avc444LumaImage) != 0 ||
	    OH_NativeImage_UpdateSurfaceImage((OH_NativeImage*)compositor->avc444ChromaImage) != 0)
	{
		ohos_compositor_format_message(message, messageSize,
		                               "update AVC444 NativeImage texture failed");
		return FALSE;
	}
	if (!ohos_compositor_ensure_program_locked(compositor, message, messageSize))
		return FALSE;

	static const GLfloat vertices[] = {
		-1.0f, -1.0f, 0.0f, 1.0f,
		1.0f,  -1.0f, 1.0f, 1.0f,
		-1.0f, 1.0f,  0.0f, 0.0f,
		1.0f,  1.0f,  1.0f, 0.0f,
	};
	if (compositor->avc444Vbo == 0)
	{
		glGenBuffers(1, &compositor->avc444Vbo);
		glBindBuffer(GL_ARRAY_BUFFER, compositor->avc444Vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	}
	else
	{
		glBindBuffer(GL_ARRAY_BUFFER, compositor->avc444Vbo);
	}

	glViewport(0, 0, (GLsizei)compositor->outputWidth, (GLsizei)compositor->outputHeight);
	glDisable(GL_DEPTH_TEST);
	glUseProgram(compositor->avc444Program);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, compositor->avc444LumaTexture);
	glUniform1i(compositor->avc444LumaUniform, 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, compositor->avc444ChromaTexture);
	glUniform1i(compositor->avc444ChromaUniform, 1);
	if (compositor->avc444OpUniform >= 0)
		glUniform1i(compositor->avc444OpUniform, (GLint)op);
	if (compositor->avc444CodecUniform >= 0)
		glUniform1i(compositor->avc444CodecUniform, (GLint)codecId);

	glEnableVertexAttribArray((GLuint)compositor->avc444PositionAttrib);
	glEnableVertexAttribArray((GLuint)compositor->avc444TexCoordAttrib);
	glVertexAttribPointer((GLuint)compositor->avc444PositionAttrib, 2, GL_FLOAT, GL_FALSE,
	                      4 * sizeof(GLfloat), (const void*)0);
	glVertexAttribPointer((GLuint)compositor->avc444TexCoordAttrib, 2, GL_FLOAT, GL_FALSE,
	                      4 * sizeof(GLfloat), (const void*)(2 * sizeof(GLfloat)));
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glDisableVertexAttribArray((GLuint)compositor->avc444PositionAttrib);
	glDisableVertexAttribArray((GLuint)compositor->avc444TexCoordAttrib);

	const GLenum glError = glGetError();
	if (glError != GL_NO_ERROR)
	{
		ohos_compositor_format_message(message, messageSize,
		                               "draw AVC444 compositor failed: gl=0x%08" PRIX32,
		                               (UINT32)glError);
		return FALSE;
	}
	if (!eglSwapBuffers(display, compositor->outputEglSurface))
	{
		ohos_compositor_format_message(message, messageSize,
		                               "swap AVC444 compositor failed: egl=0x%08" PRIX32,
		                               (UINT32)eglGetError());
		return FALSE;
	}

	compositor->mode = FREERDP_OHOS_COMPOSITOR_MODE_AVC444_GPU;
	ohos_compositor_format_message(message, messageSize,
	                               "AVC444 compositor rendered: %ux%u op=%" PRIu32
	                               " codec=%" PRIu32,
	                               compositor->outputWidth, compositor->outputHeight, op,
	                               codecId);
	return TRUE;
}

freerdpOhosCompositor* freerdp_ohos_compositor_new(void)
{
	freerdpOhosCompositor* compositor =
	    (freerdpOhosCompositor*)calloc(1, sizeof(freerdpOhosCompositor));
	if (!compositor)
		return NULL;

	InitializeCriticalSection(&compositor->lock);
	compositor->initialized = TRUE;
	compositor->mode = FREERDP_OHOS_COMPOSITOR_MODE_NONE;
	compositor->outputEglSurface = EGL_NO_SURFACE;
	compositor->avc444PositionAttrib = -1;
	compositor->avc444TexCoordAttrib = -1;
	compositor->avc444LumaUniform = -1;
	compositor->avc444ChromaUniform = -1;
	compositor->avc444OpUniform = -1;
	compositor->avc444CodecUniform = -1;
	return compositor;
}

void freerdp_ohos_compositor_free(freerdpOhosCompositor* compositor)
{
	if (!compositor)
		return;
	if (compositor->initialized)
	{
		EnterCriticalSection(&compositor->lock);
		ohos_compositor_destroy_gl_locked(compositor);
		LeaveCriticalSection(&compositor->lock);
	}
	if (compositor->initialized)
		DeleteCriticalSection(&compositor->lock);
	free(compositor);
}

BOOL freerdp_ohos_compositor_configure(freerdpOhosCompositor* compositor,
                                       const FREERDP_OHOS_COMPOSITOR_CONFIG* config,
                                       char* message, size_t messageSize)
{
	if (!compositor || !config)
	{
		ohos_compositor_format_message(message, messageSize,
		                               "OHOS compositor configure input invalid");
		return FALSE;
	}

	EnterCriticalSection(&compositor->lock);
	compositor->log = config->log;
	compositor->userData = config->userData;
	LeaveCriticalSection(&compositor->lock);

	ohos_compositor_format_message(message, messageSize,
	                               "OHOS compositor configured: log=%s",
	                               config->log ? "yes" : "no");
	return TRUE;
}

void freerdp_ohos_compositor_reset(freerdpOhosCompositor* compositor)
{
	if (!compositor)
		return;

	EnterCriticalSection(&compositor->lock);
	ohos_compositor_destroy_gl_locked(compositor);
	compositor->outputWindow = NULL;
	compositor->outputWidth = 0;
	compositor->outputHeight = 0;
	compositor->outputGeneration = 0;
	compositor->mode = FREERDP_OHOS_COMPOSITOR_MODE_NONE;
	compositor->outputTargetSets = 0;
	compositor->outputTargetClears = 0;
	compositor->avc420Begins = 0;
	compositor->avc420Ends = 0;
	compositor->avc444SurfaceSets = 0;
	compositor->avc444SurfaceClears = 0;
	compositor->avc444Frames = 0;
	compositor->avc444RenderAttempts = 0;
	compositor->avc444RenderSuccess = 0;
	compositor->avc444RenderFailures = 0;
	compositor->avc444SurfacesReady = FALSE;
	compositor->avc444Width = 0;
	compositor->avc444Height = 0;
	compositor->avc444LumaImage = NULL;
	compositor->avc444ChromaImage = NULL;
	compositor->avc444EglDisplay = NULL;
	compositor->avc444EglConfig = NULL;
	compositor->avc444EglContext = NULL;
	compositor->avc444LumaTexture = 0;
	compositor->avc444ChromaTexture = 0;
	compositor->avc444LumaSurfaceId = 0;
	compositor->avc444ChromaSurfaceId = 0;
	compositor->lastAvc444SurfaceId = 0;
	compositor->lastAvc444Width = 0;
	compositor->lastAvc444Height = 0;
	compositor->lastAvc444Op = 0;
	compositor->lastAvc444CodecId = 0;
	compositor->lastRenderError[0] = '\0';
	compositor->diagnostics[0] = '\0';
	LeaveCriticalSection(&compositor->lock);
}

BOOL freerdp_ohos_compositor_set_output_target(
    freerdpOhosCompositor* compositor, const FREERDP_OHOS_COMPOSITOR_OUTPUT_TARGET* target,
    char* message, size_t messageSize)
{
	if (!compositor || !target)
	{
		ohos_compositor_format_message(message, messageSize,
		                               "OHOS compositor output target input invalid");
		return FALSE;
	}
	if (!target->window || (target->width == 0) || (target->height == 0))
	{
		ohos_compositor_format_message(message, messageSize,
		                               "OHOS compositor output target is not ready");
		return FALSE;
	}

	UINT32 generation = 0;
	EnterCriticalSection(&compositor->lock);
	if ((compositor->outputWindow != target->window) ||
	    (compositor->outputWidth != target->width) ||
	    (compositor->outputHeight != target->height))
	{
		ohos_compositor_destroy_gl_locked(compositor);
	}
	compositor->outputWindow = target->window;
	compositor->outputWidth = target->width;
	compositor->outputHeight = target->height;
	generation = ++compositor->outputGeneration;
	compositor->outputTargetSets++;
	LeaveCriticalSection(&compositor->lock);

	ohos_compositor_format_message(message, messageSize,
	                               "OHOS compositor output target set: %ux%u generation=%" PRIu32,
	                               target->width, target->height, generation);
	return TRUE;
}

BOOL freerdp_ohos_compositor_clear_output_target(freerdpOhosCompositor* compositor,
                                                 char* message, size_t messageSize)
{
	if (!compositor)
	{
		ohos_compositor_format_message(message, messageSize,
		                               "OHOS compositor output target input invalid");
		return FALSE;
	}

	EnterCriticalSection(&compositor->lock);
	ohos_compositor_destroy_gl_locked(compositor);
	compositor->outputWindow = NULL;
	compositor->outputWidth = 0;
	compositor->outputHeight = 0;
	compositor->mode = FREERDP_OHOS_COMPOSITOR_MODE_NONE;
	compositor->outputTargetClears++;
	LeaveCriticalSection(&compositor->lock);

	ohos_compositor_format_message(message, messageSize,
	                               "OHOS compositor output target cleared");
	return TRUE;
}

BOOL freerdp_ohos_compositor_begin_avc420_surface(freerdpOhosCompositor* compositor,
                                                  char* message, size_t messageSize)
{
	if (!compositor)
	{
		ohos_compositor_format_message(message, messageSize,
		                               "OHOS compositor AVC420 input invalid");
		return FALSE;
	}

	UINT32 width = 0;
	UINT32 height = 0;
	UINT32 generation = 0;
	EnterCriticalSection(&compositor->lock);
	if (!compositor->outputWindow || (compositor->outputWidth == 0) ||
	    (compositor->outputHeight == 0))
	{
		LeaveCriticalSection(&compositor->lock);
		ohos_compositor_format_message(message, messageSize,
		                               "OHOS compositor AVC420 route unavailable: no output target");
		return FALSE;
	}
	compositor->mode = FREERDP_OHOS_COMPOSITOR_MODE_AVC420_SURFACE;
	compositor->avc420Begins++;
	width = compositor->outputWidth;
	height = compositor->outputHeight;
	generation = compositor->outputGeneration;
	LeaveCriticalSection(&compositor->lock);

	ohos_compositor_format_message(
	    message, messageSize,
	    "OHOS compositor AVC420 surface route ready: target=%ux%u generation=%" PRIu32
	    " appRendererReleaseRequired=yes",
	    width, height, generation);
	ohos_compositor_log(compositor, "%s", message ? message : "OHOS compositor AVC420 ready");
	return TRUE;
}

void freerdp_ohos_compositor_end_avc420_surface(freerdpOhosCompositor* compositor)
{
	if (!compositor)
		return;

	EnterCriticalSection(&compositor->lock);
	if (compositor->mode == FREERDP_OHOS_COMPOSITOR_MODE_AVC420_SURFACE)
		compositor->mode = FREERDP_OHOS_COMPOSITOR_MODE_NONE;
	compositor->avc420Ends++;
	LeaveCriticalSection(&compositor->lock);
}

BOOL freerdp_ohos_compositor_set_avc444_decode_surfaces(
    freerdpOhosCompositor* compositor, const FREERDP_OHOS_AVC444_SURFACE_TARGETS* targets,
    BOOL enabled, char* message, size_t messageSize)
{
	if (!compositor)
	{
		ohos_compositor_format_message(message, messageSize,
		                               "OHOS compositor AVC444 surface input invalid");
		return FALSE;
	}

	if (!enabled)
	{
		EnterCriticalSection(&compositor->lock);
		ohos_compositor_destroy_gl_locked(compositor);
		compositor->avc444SurfacesReady = FALSE;
		compositor->avc444Width = 0;
		compositor->avc444Height = 0;
		compositor->avc444LumaImage = NULL;
		compositor->avc444ChromaImage = NULL;
		compositor->avc444EglDisplay = NULL;
		compositor->avc444EglConfig = NULL;
		compositor->avc444EglContext = NULL;
		compositor->avc444LumaTexture = 0;
		compositor->avc444ChromaTexture = 0;
		compositor->avc444LumaSurfaceId = 0;
		compositor->avc444ChromaSurfaceId = 0;
		compositor->avc444SurfaceClears++;
		LeaveCriticalSection(&compositor->lock);
		ohos_compositor_format_message(message, messageSize,
		                               "OHOS compositor AVC444 decode surfaces cleared");
		return TRUE;
	}

	if (!targets || !targets->lumaWindow || !targets->chromaWindow || (targets->width == 0) ||
	    (targets->height == 0) || !targets->lumaImage || !targets->chromaImage ||
	    !targets->eglDisplay || !targets->eglConfig || !targets->eglContext)
	{
		ohos_compositor_format_message(message, messageSize,
		                               "OHOS compositor AVC444 decode surfaces are not ready");
		return FALSE;
	}

	EnterCriticalSection(&compositor->lock);
	if ((compositor->avc444EglDisplay != targets->eglDisplay) ||
	    (compositor->avc444EglContext != targets->eglContext) ||
	    (compositor->avc444EglConfig != targets->eglConfig))
	{
		ohos_compositor_destroy_gl_locked(compositor);
	}
	compositor->avc444SurfacesReady = TRUE;
	compositor->avc444Width = targets->width;
	compositor->avc444Height = targets->height;
	compositor->avc444LumaImage = targets->lumaImage;
	compositor->avc444ChromaImage = targets->chromaImage;
	compositor->avc444EglDisplay = targets->eglDisplay;
	compositor->avc444EglConfig = targets->eglConfig;
	compositor->avc444EglContext = targets->eglContext;
	compositor->avc444LumaTexture = targets->lumaTexture;
	compositor->avc444ChromaTexture = targets->chromaTexture;
	compositor->avc444LumaSurfaceId = targets->lumaSurfaceId;
	compositor->avc444ChromaSurfaceId = targets->chromaSurfaceId;
	compositor->avc444SurfaceSets++;
	LeaveCriticalSection(&compositor->lock);

	ohos_compositor_format_message(
	    message, messageSize,
	    "OHOS compositor AVC444 decode surfaces set: %ux%u lumaTex=%" PRIu32
	    " chromaTex=%" PRIu32 " lumaSurface=%" PRIu64 " chromaSurface=%" PRIu64
	    " egl=%s route=disabled-until-gpu-shader",
	    targets->width, targets->height, targets->lumaTexture, targets->chromaTexture,
	    targets->lumaSurfaceId, targets->chromaSurfaceId,
	    (targets->eglDisplay && targets->eglContext) ? "ready" : "none");
	return TRUE;
}

void freerdp_ohos_compositor_notify_avc444_frame(freerdpOhosCompositor* compositor,
                                                 UINT32 surfaceId, UINT32 width, UINT32 height,
                                                 UINT32 op, UINT32 codecId)
{
	if (!compositor)
		return;

	char message[256] = { 0 };
	BOOL rendered = FALSE;
	UINT64 attempts = 0;
	UINT64 successes = 0;
	UINT64 failures = 0;
	EnterCriticalSection(&compositor->lock);
	compositor->avc444Frames++;
	compositor->lastAvc444SurfaceId = surfaceId;
	compositor->lastAvc444Width = width;
	compositor->lastAvc444Height = height;
	compositor->lastAvc444Op = op;
	compositor->lastAvc444CodecId = codecId;
	attempts = ++compositor->avc444RenderAttempts;
	rendered = ohos_compositor_render_avc444_locked(compositor, op, codecId, message,
	                                                sizeof(message));
	if (rendered)
	{
		successes = ++compositor->avc444RenderSuccess;
		compositor->lastRenderError[0] = '\0';
	}
	else
	{
		failures = ++compositor->avc444RenderFailures;
		ohos_compositor_set_render_error_locked(compositor, "%s",
		                                        message[0] ? message : "unknown");
	}
	LeaveCriticalSection(&compositor->lock);

	if (rendered)
	{
		if ((successes <= 3) || ((successes % 60) == 0))
			ohos_compositor_log(compositor, "%s attempts=%" PRIu64 " success=%" PRIu64,
			                    message, attempts, successes);
	}
	else if ((failures <= 3) || ((failures % 60) == 0))
	{
		ohos_compositor_log(compositor, "AVC444 compositor render failed: %s attempts=%" PRIu64
		                                " failures=%" PRIu64,
		                    message[0] ? message : "unknown", attempts, failures);
	}
}

const char* freerdp_ohos_compositor_get_diagnostics(freerdpOhosCompositor* compositor)
{
	if (!compositor)
		return "ohos compositor: unavailable";

	EnterCriticalSection(&compositor->lock);
	(void)snprintf(
	    compositor->diagnostics, sizeof(compositor->diagnostics),
	    "ohos compositor: mode=%s output=%s:%ux%u generation=%" PRIu32
	    " target=set:%" PRIu64 ",clear:%" PRIu64
	    " avc420=begin:%" PRIu64 ",end:%" PRIu64
	    " avc444Surfaces=%s:%ux%u set:%" PRIu64 ",clear:%" PRIu64
	    " lumaTex=%" PRIu32 " chromaTex=%" PRIu32 " images=%s egl=%s"
	    " lumaSurface=%" PRIu64 " chromaSurface=%" PRIu64
	    " avc444Frames=%" PRIu64 " lastAvc444=surface:%" PRIu32
	    " size:%" PRIu32 "x%" PRIu32 " op:%" PRIu32 " codec:%" PRIu32
	    " render=attempt:%" PRIu64 ",success:%" PRIu64 ",failure:%" PRIu64
	    " error=%s",
	    ohos_compositor_mode_name(compositor->mode),
	    compositor->outputWindow ? "ready" : "none", compositor->outputWidth,
	    compositor->outputHeight, compositor->outputGeneration, compositor->outputTargetSets,
	    compositor->outputTargetClears, compositor->avc420Begins, compositor->avc420Ends,
	    compositor->avc444SurfacesReady ? "ready" : "none", compositor->avc444Width,
	    compositor->avc444Height, compositor->avc444SurfaceSets,
	    compositor->avc444SurfaceClears, compositor->avc444LumaTexture,
	    compositor->avc444ChromaTexture,
	    (compositor->avc444LumaImage && compositor->avc444ChromaImage) ? "ready" : "none",
	    (compositor->avc444EglDisplay && compositor->avc444EglContext) ? "ready" : "none",
	    compositor->avc444LumaSurfaceId,
	    compositor->avc444ChromaSurfaceId, compositor->avc444Frames,
	    compositor->lastAvc444SurfaceId, compositor->lastAvc444Width,
	    compositor->lastAvc444Height, compositor->lastAvc444Op,
	    compositor->lastAvc444CodecId, compositor->avc444RenderAttempts,
	    compositor->avc444RenderSuccess, compositor->avc444RenderFailures,
	    compositor->lastRenderError[0] ? compositor->lastRenderError : "none");
	LeaveCriticalSection(&compositor->lock);
	return compositor->diagnostics;
}
