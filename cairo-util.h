#ifndef _CAIRO_UTIL_H
#define _CAIRO_UTIL_H

#include "wayland-client.h"

struct wl_buffer *
wl_buffer_create_from_cairo_surface(struct wl_display *display,
				    cairo_surface_t *surface);

void
blur_surface(cairo_surface_t *surface, int margin);

#endif
