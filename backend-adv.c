#include <stdio.h>
#include <stdlib.h>

#include "wayland.h"
#include "wayland-backend.h"
#include "wayland-internal.h"

struct wl_backend_advertisement {
	struct wl_object base;
	struct wl_backend *backend;
};

static void
wl_backend_advertisement_request_info(struct wl_client *client,
				      struct wl_object *base)
{
	struct wl_backend_advertisement *object;

	object = (struct wl_backend_advertisement *) base;
	wl_display_send_event (client->display, base, 0,
			       object->backend->backend_name,
			       object->backend->args);
}

static const struct wl_event backend_advertisement_events[] = {
	WL_DEFEVENT ("reply_info", "ss")
};

static const struct wl_method backend_advertisement_methods[] = {
	WL_DEFMETHOD ("request_info", "", wl_backend_advertisement_request_info)
};

static const struct wl_interface backend_advertisement_interface = {
	"backend_advertisement", 1,
	ARRAY_LENGTH(backend_advertisement_methods),
	backend_advertisement_methods,
	ARRAY_LENGTH(backend_advertisement_events),
	backend_advertisement_events,
};

struct wl_object *
wl_backend_advertisement_create(struct wl_display *display, uint32_t id)
{
	struct wl_backend_advertisement *backend_adv;

	backend_adv = malloc(sizeof *backend_adv);
	if (backend_adv == NULL)
		return NULL;

	backend_adv->base.id = id;
	backend_adv->base.interface = &backend_advertisement_interface;
	backend_adv->backend = display->backend;
	return &backend_adv->base;
}
