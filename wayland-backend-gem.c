#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <i915_drm.h>
#include <sys/ioctl.h>

#include "wayland-client.h"
#include "wayland-backend.h"
#include "wayland-backend-internal.h"

struct wl_buffer_private {
        struct wl_buffer public;
        int handle;
};

struct wl_backend_private {
        struct wl_backend public;
	const char *device;
	const char *driver;
        int fd;
};

static int
wl_gem_destroy (struct wl_backend *b)
{
        struct wl_backend_private *backend = (struct wl_backend_private *) b;
        int rc;
        rc = close (backend->fd);
        free (backend->public.args);
        free (backend->device);
        free (backend->driver);
        free (backend);
        return rc;
}

static struct wl_buffer *
wl_gem_buffer_create(struct wl_backend *b, int width, int height, int stride)
{
	struct wl_buffer_private *buffer;
	struct wl_backend_private *backend = (struct wl_backend_private *) b;
	struct drm_i915_gem_create create;
	struct drm_gem_flink flink;

	buffer = malloc(sizeof *buffer);
	buffer->public.backend = b;
	buffer->public.width = width;
	buffer->public.height = height;
	buffer->public.stride = stride;

	memset(&create, 0, sizeof(create));
	create.size = height * stride;

	if (ioctl(backend->fd, DRM_IOCTL_I915_GEM_CREATE, &create) != 0) {
		fprintf(stderr, "gem create failed: %m\n");
		free(buffer);
		return NULL;
	}

	flink.handle = create.handle;
	if (ioctl(backend->fd, DRM_IOCTL_GEM_FLINK, &flink) != 0) {
		fprintf(stderr, "gem flink failed: %m\n");
		free(buffer);
		return 0;
	}

	buffer->handle = flink.handle;
	buffer->public.name = flink.name;

	return &buffer->public;
}

static int
wl_gem_buffer_destroy(struct wl_buffer *b)
{
	struct drm_gem_close close;
	struct wl_buffer_private *buffer = (struct wl_buffer_private *) b;
	struct wl_backend_private *backend = (struct wl_backend_private *) b->backend;

	close.handle = buffer->handle;
	if (ioctl(backend->fd, DRM_IOCTL_GEM_CLOSE, &close) < 0) {
		fprintf(stderr, "gem close failed: %m\n");
		return -1;
	}
	
	free (b);

	return 0;
}

static int
wl_gem_buffer_data(struct wl_buffer *b, void *data)
{
	struct drm_i915_gem_pwrite pwrite;
	struct wl_buffer_private *buffer = (struct wl_buffer_private *) b;
	struct wl_backend_private *backend = (struct wl_backend_private *) b->backend;

	pwrite.handle = buffer->handle;
	pwrite.offset = 0;
	pwrite.size = buffer->public.height * buffer->public.stride;
	pwrite.data_ptr = (uint64_t) (uintptr_t) data;

	if (ioctl(backend->fd, DRM_IOCTL_I915_GEM_PWRITE, &pwrite) < 0) {
		fprintf(stderr, "gem pwrite failed: %m\n");
		return -1;
	}

	return 0;
}

static EGLDisplay
wl_gem_get_egl_display (struct wl_backend *b)
{
	struct wl_backend_private *backend = (struct wl_backend_private *) b;
	return eglCreateDisplayNative(backend->device, backend->driver);
}

static EGLSurface
wl_gem_get_egl_surface (struct wl_backend *backend, EGLConfig config,
			int name, int width, int height, int strude)
{
	return eglCreateSurfaceForName(backend->egl_display, config,
				       buffer->name,
                                       buffer->width, buffer->height,
                                       buffer->stride, NULL);
}

#define GEM_DEVICE			"/dev/dri/card0"

struct wl_backend *
wl_gem_open (const char *args)
{
	struct wl_backend_private *backend;
	char *device, *driver;
	const char *p;
        int fd = -1;

	if (args == NULL)
		args = "i965:" GEM_DEVICE;
	p = strchr (args, ':');
	if (p) {
		device = strndup (args, p - args);
		driver = strdup (p + 1);
	} else {
		device = strdup (args);
		driver = strdup (GEM_DEVICE);
	}

	if (device == NULL || driver == NULL)
		goto fail;

	fd = open(device, O_RDWR);
        if (fd == -1)
		goto fail;

	backend = malloc (sizeof *backend);
	if (backend == NULL)
		goto fail;

	memset (backend, 0, sizeof *backend);
	backend->device = device;
	backend->driver = driver;
	backend->fd = fd;
	backend->public.backend_name = "eagle";
	backend->public.args = strdup (args);
	if (backend->public.args == NULL)
		goto fail;

	backend->public.destroy = wl_gem_destroy;
	backend->public.get_egl_display = wl_gem_get_egl_display;
	backend->public.get_egl_surface = wl_gem_get_egl_surface;
	backend->public.buffer_create = wl_gem_buffer_create;
	backend->public.buffer_data = wl_gem_buffer_data;
	backend->public.buffer_destroy = wl_gem_buffer_destroy;
	return &backend->public;

fail:
	free (device);
	free (driver);
	if (fd != -1)
		close (fd);
	if (backend) {
		free (backend->public.args);
		free (backend);
	}
	return NULL;
}
