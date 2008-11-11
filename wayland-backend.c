#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "wayland-client.h"
#include "wayland-backend.h"
#include "wayland-backend-internal.h"

WL_EXPORT void
wl_backend_destroy(struct wl_backend *backend)
{
	backend->destroy (backend);
}

WL_EXPORT EGLDisplay
wl_backend_get_egl_display(struct wl_backend *backend)
{
	if (backend->egl_display == NULL && backend->get_egl_display != NULL)
		return backend->get_egl_display(backend);

	return egl_display;
}

WL_EXPORT EGLSurface
wl_backend_get_egl_surface(struct wl_backend *backend, EGLConfig config,
			   int name, int width, int height, int stride)
{
	if (wl_backend_get_egl_display(backend) == NULL
	    || backend->buffer_get_egl_surface == NULL)
		return NULL;

	return backend->buffer_get_egl_surface(backend, config, name,
					       width, height, stride);
}

WL_EXPORT EGLSurface
wl_buffer_get_egl_surface(struct wl_buffer *buffer, EGLConfig config)
{
	return wl_backend_get_egl_surface(buffer->backend, display, config,
					  buffer->name, buffer->width,
					  buffer->height, buffer->stride);
}

WL_EXPORT EGLSurface
wl_buffer_get_egl_surface(struct wl_buffer *buffer, EGLDisplay display,
			  EGLConfig config)
{
	if (buffer->backend->buffer_get_egl_surface)
		return buffer->backend->buffer_get_egl_surface(buffer, display,
							       config);
	else
		return NULL;
}

WL_EXPORT struct wl_buffer *
wl_backend_create_buffer(struct wl_backend *backend,
			 int width, int height, int stride)
{
	return backend->buffer_create (backend, width, height, stride);
}

WL_EXPORT struct wl_buffer *
wl_backend_create_buffer_from_data(struct wl_backend *backend,
				   int width, int height,
				   int stride, void *data)
{
	struct wl_buffer *buffer;
	buffer = wl_backend_create_buffer(backend, width, height, stride);
	if (buffer == NULL)
		return NULL;

	if (wl_buffer_data(buffer, data) < 0) {
		wl_buffer_destroy(buffer);
		return NULL;
	}			

	return buffer;
}

WL_EXPORT int
wl_buffer_destroy(struct wl_buffer *buffer)
{
	return buffer->backend->buffer_destroy (buffer);
}

WL_EXPORT int
wl_buffer_data(struct wl_buffer *buffer, void *data)
{
	return buffer->backend->buffer_data (buffer, data);
}


/* Backend factory.  */

struct wl_backend *
wl_backend_create (const char *name, const char *args)
{
	if (!strcmp (name, "gem"))
		return wl_gem_open (args);
	return NULL;
}
