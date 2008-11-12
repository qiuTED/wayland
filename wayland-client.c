#include <alloca.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <ctype.h>
#include <sys/poll.h>

#include "connection.h"
#include "wayland-client.h"
#include "wayland-backend.h"

static const char socket_name[] = "\0wayland";

struct wl_display {
	struct wl_proxy proxy;
	struct wl_proxy *global_objects;
	struct wl_connection *connection;
	struct wl_backend *backend;
	int fd;
	uint32_t id;
	uint32_t mask;

	wl_display_update_func_t update;
	void *update_data;

	wl_display_event_func_t event_handler;
	void *event_handler_data;
};

struct wl_surface {
	struct wl_proxy proxy;
};

static int
connection_update(struct wl_connection *connection,
		  uint32_t mask, void *data)
{
	struct wl_display *display = data;

	display->mask = mask;
	if (display->update)
		return display->update(display->mask,
				       display->update_data);

	return 0;
}

static int
wl_display_read_proxy (struct wl_display *display, struct wl_proxy *proxy)
{
	uint32_t id;
	wl_connection_sync(display->connection);

	if (!proxy)
		proxy = malloc (sizeof *proxy);

	proxy->id =
		wl_connection_demarshal_mem(display->connection, NULL, proxy,
					    sizeof *proxy, "ppi|s", display,
					    display->global_objects, 0);
	display->global_objects = proxy;
	return id;
}

WL_EXPORT struct wl_proxy *
wl_display_get_interface(struct wl_display *display, const char *interface,
			 struct wl_proxy *prev)
{
	struct wl_proxy *proxy;
	proxy = prev ? prev->next : display->global_objects;
	while (proxy && strcmp (proxy->interface, interface) != 0)
		proxy = proxy->next;

	return proxy;
}

static struct wl_backend *
wl_display_create_backend(struct wl_display *display, struct wl_proxy *backend_adv)
{
	uint32_t id;
	struct wl_backend *be;
	struct {
		char *device, *driver;
	} reply;

	wl_connection_marshal(display->connection, NULL, backend_adv->id,
			      0, "");
	wl_connection_data(display->connection, WL_CONNECTION_WRITABLE);
	wl_connection_sync(display->connection);

	/* FIXME: we expect no other events to arrive.  */
	id = wl_connection_demarshal_mem(display->connection, NULL,
				         &reply, sizeof reply, "|ss");
	if (id != backend_adv->id)
		abort ();

	be = _wl_backend_create (reply.device, reply.driver, 0);
	free (reply.device);
	free (reply.driver);
	return be;
}

WL_EXPORT struct wl_display *
wl_display_create(const char *address)
{
	struct wl_display *display;
	struct wl_proxy *backend_adv;
	struct sockaddr_un name;
	socklen_t size;
	size_t n;

	display = malloc(sizeof *display);
	if (display == NULL)
		return NULL;

	memset(display, 0, sizeof *display);
	display->fd = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (display->fd < 0)
		goto fail;

	name.sun_family = AF_LOCAL;
	memcpy(name.sun_path, address, strlen(address + 1) + 2);

	size = offsetof (struct sockaddr_un, sun_path) + sizeof socket_name;

	if (connect(display->fd, (struct sockaddr *) &name, size) < 0)
		goto fail;

	/* FIXME: We'll need a protocol for getting a new range, I
	 * guess... */
	read(display->fd, &display->id, sizeof display->id);

	display->connection = wl_connection_create(display->fd,
						   connection_update,
						   display);

	read(display->fd, &n, sizeof n);
	wl_display_read_proxy (display, &display->proxy);
	while (n--)
		wl_display_read_proxy (display, NULL);

	for (backend_adv = NULL; ; ) {
		backend_adv = wl_display_get_interface (display,
							"backend_advertisement",
							backend_adv);
		if (backend_adv == NULL)
			break;

		display->backend = wl_display_create_backend (display, backend_adv);
		if (display->backend != NULL)
			break;
	}

	if (display->backend == NULL)
		goto fail;

	return display;

fail:
	if (display && display->backend)
		wl_backend_destroy(display->backend);
	if (display && display->fd != -1)
		close(display->fd);
	free(display);
	return NULL;
}

WL_EXPORT void
wl_display_destroy(struct wl_display *display)
{
	while (display->global_objects) {
		struct wl_proxy *next = display->global_objects->next;

		/* Watch out, the display itself is one of the
		   global objects.  */
		if (display->global_objects != &display->proxy)
			free (display->global_objects);
		display->global_objects = next;
	}

	wl_backend_destroy(display->backend);
	wl_connection_destroy(display->connection);
	close(display->fd);
	free(display);
}

WL_EXPORT const char *
wl_display_get_backend_name (struct wl_display *display)
{
	return display->backend->backend_name;
}

WL_EXPORT const char *
wl_display_get_backend_args (struct wl_display *display)
{
	return display->backend->args;
}

WL_EXPORT int
wl_display_get_fd(struct wl_display *display,
		  wl_display_update_func_t update, void *data)
{
	display->update = update;
	display->update_data = data;

	display->update(display->mask, display->update_data);

	return display->fd;
}

static void
handle_event(struct wl_display *display, uint32_t id, uint32_t opcode,
	     uint32_t size)
{
	if (size < 4096) {
		uint32_t *p = alloca (size);

		wl_connection_copy(display->connection, p, size);
		if (display->event_handler != NULL)
			display->event_handler(display, id, opcode, p[2], p[3],
					       display->event_handler_data);
	}

	wl_connection_consume(display->connection, size);
}

WL_EXPORT void
wl_display_iterate(struct wl_display *display, uint32_t mask)
{
	uint32_t p[2], opcode, size;
	int len;

	len = wl_connection_data(display->connection, mask);
	while (len > 0) {
		if (len < sizeof p)
			break;
		
		wl_connection_copy(display->connection, p, sizeof p);
		opcode = p[1] & 0xffff;
		size = p[1] >> 16;
		if (len < size)
			break;

		handle_event(display, p[0], opcode, size);
		len -= size;
	}

	if (len < 0) {
		fprintf(stderr, "read error: %m\n");
		exit(EXIT_FAILURE);
	}
}

WL_EXPORT void
wl_display_set_event_handler(struct wl_display *display,
			     wl_display_event_func_t handler,
			     void *data)
{
	/* FIXME: This needs something more generic... */
	display->event_handler = handler;
	display->event_handler_data = data;
}

#define WL_DISPLAY_CREATE_SURFACE 0

WL_EXPORT struct wl_surface *
wl_display_create_surface(struct wl_display *display)
{
	struct wl_surface *surface;

	surface = malloc(sizeof *surface);
	if (surface == NULL)
		return NULL;

	surface->proxy.id = display->id++;
	surface->proxy.display = display;

	wl_connection_marshal(display->connection, NULL, display->proxy.id,
			      WL_DISPLAY_CREATE_SURFACE, "O", surface->proxy.id);
	return surface;
}

WL_EXPORT EGLDisplay
wl_display_get_egl_display(struct wl_display *display)
{
	return wl_backend_get_egl_display (display->backend);
}

WL_EXPORT struct wl_buffer *
wl_display_create_buffer(struct wl_display *display,
			 int width, int height, int stride)
{
	return wl_backend_create_buffer (display->backend,
					 width, height, stride);
}

WL_EXPORT struct wl_buffer *
wl_display_create_buffer_from_data(struct wl_display *display,
				   int width, int height,
				   int stride, void *data)
{
	return wl_backend_create_buffer_from_data (display->backend,
						   width, height, stride, data);
}


#define WL_SURFACE_DESTROY	0
#define WL_SURFACE_ATTACH	1
#define WL_SURFACE_MAP		2
#define WL_SURFACE_COPY		3
#define WL_SURFACE_DAMAGE	4

WL_EXPORT void
wl_surface_destroy(struct wl_surface *surface)
{
	wl_connection_marshal(surface->proxy.display->connection, NULL,
			      surface->proxy.id, WL_SURFACE_DESTROY, "");
}

WL_EXPORT void
wl_surface_attach(struct wl_surface *surface, uint32_t name,
		  int32_t width, int32_t height, uint32_t stride)
{
	wl_connection_marshal(surface->proxy.display->connection, NULL,
			      surface->proxy.id, WL_SURFACE_ATTACH, "iiii",
			      name, width, height, stride);
}

WL_EXPORT void
wl_surface_map(struct wl_surface *surface,
	       int32_t x, int32_t y, int32_t width, int32_t height)
{
	wl_connection_marshal(surface->proxy.display->connection, NULL,
			      surface->proxy.id, WL_SURFACE_MAP, "iiii",
			      x, y, width, height);
}

WL_EXPORT void
wl_surface_copy(struct wl_surface *surface, int32_t dst_x, int32_t dst_y,
		uint32_t name, uint32_t stride,
		int32_t x, int32_t y, int32_t width, int32_t height)
{
	wl_connection_marshal(surface->proxy.display->connection, NULL,
			      surface->proxy.id, WL_SURFACE_COPY, "iiiiiiii",
			      dst_x, dst_y, name, stride, x, y, width, height);
}

WL_EXPORT void
wl_surface_damage(struct wl_surface *surface,
		  int32_t x, int32_t y, int32_t width, int32_t height)
{
	wl_connection_marshal(surface->proxy.display->connection, NULL,
			      surface->proxy.id, WL_SURFACE_DAMAGE, "iiii",
			      x, y, width, height);
}


/* Higher-level APIs.  */

WL_EXPORT void
wl_surface_attach_buffer(struct wl_surface *surface,
			 struct wl_buffer *buffer)
{
	return wl_surface_attach(surface, buffer->name,
				 buffer->width, buffer->height, buffer->stride);
}


WL_EXPORT void
wl_surface_copy_buffer(struct wl_surface *surface, int32_t dst_x, int32_t dst_y,
		       struct wl_buffer *src,
		       int32_t x, int32_t y, int32_t width, int32_t height)
{
	wl_surface_copy (surface, dst_x, dst_y, src->name, src->stride,
			 x, y, width, height);
}

