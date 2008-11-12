#ifndef _WAYLAND_INTERNAL_H
#define _WAYLAND_INTERNAL_H

#include "hash.h"

struct wl_client {
	struct wl_connection *connection;
	struct wl_event_source *source;
	struct wl_display *display;
	struct wl_list object_list;
	struct wl_list link;
};

struct wl_display {
	struct wl_object base;
	struct wl_event_loop *loop;
	struct wl_hash objects;

	struct wl_backend *backend;
	struct wl_compositor *compositor;
	struct wl_compositor_interface *compositor_interface;

	struct wl_list global_objects_list;
	struct wl_list surface_list;
	struct wl_list client_list;
	uint32_t client_id_range;
};

struct wl_surface {
	struct wl_object base;

	/* provided by client */
	int width, height;
	int buffer;
	int stride;
	
	struct wl_map map;
	struct wl_list link;

	/* how to convert buffer contents to pixels in screen format;
	 * yuv->rgb, indexed->rgb, svg->rgb, but mostly just rgb->rgb. */

	/* how to transform/render rectangular contents to polygons. */

	void *compositor_data;
};

struct wl_object_ref {
	struct wl_object *object;
	struct wl_list link;
};


#endif
