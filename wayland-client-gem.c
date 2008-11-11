#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <i915_drm.h>
#include <sys/ioctl.h>
#include "wayland-client.h"

struct wl_buffer_private {
	struct wl_buffer public;
	int handle;
};

static int fd;

int
wl_gem_open (const char *gem_device)
{
	fd = open(gem_device, O_RDWR);
	return fd == -1 ? -1 : 0;
}

int
wl_gem_close (void)
{
	int rc;
	if (fd == -1)
		return -1;
	rc = close (fd);
	fd = -1;
	return rc;
}

/* FIXME: We'd need to get the stride right here in a chipset
 * independent way.  */

WL_EXPORT struct wl_buffer *
wl_buffer_create(int width, int height, int stride)
{
	struct wl_buffer_private *buffer;
	struct drm_i915_gem_create create;
	struct drm_gem_flink flink;

	buffer = malloc(sizeof *buffer);
	buffer->public.width = width;
	buffer->public.height = height;
	buffer->public.stride = stride;

	memset(&create, 0, sizeof(create));
	create.size = height * stride;

	if (ioctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create) != 0) {
		fprintf(stderr, "gem create failed: %m\n");
		free(buffer);
		return NULL;
	}

	flink.handle = create.handle;
	if (ioctl(fd, DRM_IOCTL_GEM_FLINK, &flink) != 0) {
		fprintf(stderr, "gem flink failed: %m\n");
		free(buffer);
		return 0;
	}

	buffer->handle = flink.handle;
	buffer->public.name = flink.name;

	return &buffer->public;
}

WL_EXPORT struct wl_buffer *
wl_buffer_create_from_data(int width, int height, int stride, void *data)
{
	struct wl_buffer *buffer;
	buffer = wl_buffer_create(width, height, stride);
	if (buffer == NULL)
		return NULL;

	if (wl_buffer_data(buffer, data) < 0) {
		wl_buffer_destroy(buffer);
		return NULL;
	}			

	return buffer;
}

WL_EXPORT int
wl_buffer_destroy(struct wl_buffer *b)
{
	struct drm_gem_close close;
	struct wl_buffer_private *buffer = (struct wl_buffer_private *) b;

	close.handle = buffer->handle;
	if (ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close) < 0) {
		fprintf(stderr, "gem close failed: %m\n");
		return -1;
	}
	
	free (b);

	return 0;
}

WL_EXPORT int
wl_buffer_data(struct wl_buffer *b, void *data)
{
	struct drm_i915_gem_pwrite pwrite;
	struct wl_buffer_private *buffer = (struct wl_buffer_private *) b;

	pwrite.handle = buffer->handle;
	pwrite.offset = 0;
	pwrite.size = buffer->public.height * buffer->public.stride;
	pwrite.data_ptr = (uint64_t) (uintptr_t) data;

	if (ioctl(fd, DRM_IOCTL_I915_GEM_PWRITE, &pwrite) < 0) {
		fprintf(stderr, "gem pwrite failed: %m\n");
		return -1;
	}

	return 0;
}
