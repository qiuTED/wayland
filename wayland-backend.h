#ifndef _WAYLAND_BACKEND_H
#define _WAYLAND_BACKEND_H

#include <GL/gl.h>
#include <eagle.h>

struct wl_backend {
	char *backend_name;
	char *args;
	EGLDisplay *egl_display;

	EGLDisplay (*get_egl_display) (struct wl_backend *);
	EGLSurface (*get_egl_surface) (struct wl_backend *, EGLConfig, int,
				       int, int, int);
	int (*destroy) (struct wl_backend *);
	struct wl_buffer *(*buffer_create) (struct wl_backend *, int, int, int);
	int (*buffer_data) (struct wl_buffer *, void *);
	int (*buffer_destroy) (struct wl_buffer *);
};

struct wl_buffer {
        struct wl_backend *backend;
        int width, height, stride;
        uint32_t name, handle;
};

struct wl_backend *wl_backend_create (const char *backend_name, const char *args);

struct wl_buffer *wl_backend_create_buffer (struct wl_backend *backend,
					    int width, int height, int stride);
struct wl_buffer *wl_backend_create_buffer_from_data (struct wl_backend *backend,
						      int width, int height,
						      int stride, void *data);
EGLDisplay wl_backend_get_egl_display(struct wl_backend *backend);
EGLSurface wl_backend_get_egl_surface(struct wl_backend *backend,
				      EGLConfig config, int name,
				      int width, int height, int stride);

void wl_backend_destroy(struct wl_backend *backend);

const char *wl_backend_get_name (struct wl_backend *backend);
const char *wl_backend_get_args (struct wl_backend *backend);

EGLSurface wl_buffer_get_egl_surface(struct wl_buffer *buffer,
				     EGLConfig config);
int wl_buffer_destroy(struct wl_buffer *buffer);
int wl_buffer_data(struct wl_buffer *buffer, void *data);


#endif
