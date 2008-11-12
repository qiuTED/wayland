#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/fb.h>

#include "wayland.h"
#include "wayland-backend.h"

struct lame_compositor {
	struct wl_compositor base;
	void *fb;
	int32_t width, height, stride;
	struct wl_display *wl_display;
};

static void
notify_surface_create(struct wl_compositor *compositor,
		      struct wl_surface *surface)
{
}
				   
static void
notify_surface_destroy(struct wl_compositor *compositor,
		       struct wl_surface *surface)
{
	struct wl_buffer *b;

	b = wl_surface_get_data(surface);
	if (b == NULL)
		return;
	
	wl_buffer_destroy (b);
}

static void
notify_surface_attach(struct wl_compositor *compositor,
		      struct wl_surface *surface, uint32_t name, 
		      uint32_t width, uint32_t height, uint32_t stride)
{
	struct lame_compositor *lc = (struct lame_compositor *) compositor;
	struct wl_backend *backend;
	struct wl_buffer *b;

	backend = wl_display_get_backend (lc->wl_display);
	b = wl_surface_get_data(surface);
	if (b != NULL)
		wl_buffer_destroy (b);

	b = wl_backend_open_buffer (backend, width, height, stride, name);
	wl_surface_set_data (surface, b);
}

static void
notify_surface_map(struct wl_compositor *compositor,
		   struct wl_surface *surface, struct wl_map *map)
{
	struct lame_compositor *lc = (struct lame_compositor *) compositor;
	struct wl_buffer *b;
	char *data, *dst;
	int i;

	/* This part is where we actually copy the buffer to screen.
	 * Needs to be part of the repaint loop, not in the notify_map
	 * handler. */

	b = wl_surface_get_data(surface);
	if (b == NULL)
		return;

	data = wl_buffer_get_data(b);
	if (data == NULL) {
		if (errno == ENOMEM)
			fprintf(stderr, "swap buffers malloc failed\n");
		else
			fprintf(stderr, "gem pread failed: %m\n");
		return;
	}

	dst = lc->fb + lc->stride * map->y + map->x * 4;
	for (i = 0; i < b->height; i++)
		memcpy(dst + lc->stride * i, data + b->stride * i, b->width * 4);

	wl_buffer_free_data(b, data);
}

struct wl_compositor_interface interface = {
	notify_surface_create,
	notify_surface_destroy,
	notify_surface_attach,
	notify_surface_map
};

static const char fb_device[] = "/dev/fb";

struct wl_compositor *
wl_compositor_create(struct wl_display *display)
{
	struct lame_compositor *lc;
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	int fd;

	lc = malloc(sizeof *lc);
	if (lc == NULL)
		return NULL;

	lc->base.interface = &interface;

	fd = open(fb_device, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open %s failed: %m\n", fb_device);
		return NULL;
	}

	if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0) {
		fprintf(stderr, "fb get fixed failed\n");
		return NULL;
	}

	if (ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
		fprintf(stderr, "fb get fixed failed\n");
		return NULL;
	}

	lc->stride = fix.line_length;
	lc->width = var.xres;
	lc->height = var.yres;
	lc->wl_display = display;
	lc->fb = mmap(NULL, lc->stride * lc->height,
		      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (lc->fb == MAP_FAILED) {
		fprintf(stderr, "fb map failed\n");
		return NULL;
	}

	return &lc->base;
}
