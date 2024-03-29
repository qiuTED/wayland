#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <ffi.h>

#include "wayland.h"
#include "wayland-backend.h"
#include "wayland-internal.h"
#include "hash.h"
#include "connection.h"

void wl_list_init(struct wl_list *list)
{
	list->prev = list;
	list->next = list;
}

void
wl_list_insert(struct wl_list *list, struct wl_list *elm)
{
	elm->prev = list;
	elm->next = list->next;
	list->next = elm;
	elm->next->prev = elm;
}

void
wl_list_remove(struct wl_list *elm)
{
	elm->prev->next = elm->next;
	elm->next->prev = elm->prev;
}

static volatile sig_atomic_t display_exit;

static void
sigterm_handler (int sig)
{
	display_exit = 1;
}

static void
wl_surface_destroy(struct wl_client *client,
		   struct wl_surface *surface)
{
	const struct wl_compositor_interface *interface;

	interface = client->display->compositor->interface;
	interface->notify_surface_destroy(client->display->compositor,
					  surface);
	wl_list_remove(&surface->link);
}

static void
wl_surface_attach(struct wl_client *client,
		  struct wl_surface *surface, uint32_t name, 
		  uint32_t width, uint32_t height, uint32_t stride)
{
	const struct wl_compositor_interface *interface;

	interface = client->display->compositor->interface;
	interface->notify_surface_attach(client->display->compositor,
					 surface, name, width, height, stride);
}

static void
wl_surface_map(struct wl_client *client, struct wl_surface *surface,
	       int32_t x, int32_t y, int32_t width, int32_t height)
{
	const struct wl_compositor_interface *interface;

	/* FIXME: This needs to take a tri-mesh argument... - count
	 * and a list of tris. 0 tris means unmap. */

	surface->map.x = x;
	surface->map.y = y;
	surface->map.width = width;
	surface->map.height = height;

	interface = client->display->compositor->interface;
	interface->notify_surface_map(client->display->compositor,
				      surface, &surface->map);
}

static void
wl_surface_copy(struct wl_client *client, struct wl_surface *surface,
		int32_t dst_x, int32_t dst_y, uint32_t name, uint32_t stride,
		int32_t x, int32_t y, int32_t width, int32_t height)
{
	const struct wl_compositor_interface *interface;

	interface = client->display->compositor->interface;
	interface->notify_surface_copy(client->display->compositor,
				       surface, dst_x, dst_y,
				       name, stride, x, y, width, height);
}

static void
wl_surface_damage(struct wl_client *client, struct wl_surface *surface,
		  int32_t x, int32_t y, int32_t width, int32_t height)
{
	const struct wl_compositor_interface *interface;

	interface = client->display->compositor->interface;
	interface->notify_surface_damage(client->display->compositor,
					 surface, x, y, width, height);
}

static const struct wl_method surface_methods[] = {
	WL_DEFMETHOD ("destroy", "", wl_surface_destroy)
	WL_DEFMETHOD ("attach", "iiii", wl_surface_attach)
	WL_DEFMETHOD ("map", "iiii", wl_surface_map)
	WL_DEFMETHOD ("copy", "iiiiiiii", wl_surface_copy)
	WL_DEFMETHOD ("damage", "iiii", wl_surface_damage)
};

static const struct wl_interface surface_interface = {
	"surface", 1,
	ARRAY_LENGTH(surface_methods),
	surface_methods,
};

static struct wl_surface *
wl_surface_create(struct wl_display *display, uint32_t id)
{
	struct wl_surface *surface;
	const struct wl_compositor_interface *interface;

	surface = malloc(sizeof *surface);
	if (surface == NULL)
		return NULL;

	surface->base.id = id;
	surface->base.interface = &surface_interface;

	wl_list_insert(display->surface_list.prev, &surface->link);

	interface = display->compositor->interface;
	interface->notify_surface_create(display->compositor, surface);

	return surface;
}

WL_EXPORT void
wl_surface_set_data(struct wl_surface *surface, void *data)
{
	surface->compositor_data = data;
}

WL_EXPORT void *
wl_surface_get_data(struct wl_surface *surface)
{
	return surface->compositor_data;
}

void
wl_client_destroy(struct wl_client *client);

static void
wl_client_event(struct wl_client *client, struct wl_object *object, uint32_t event)
{
	uint32_t p[2];

	p[0] = object->id;
	p[1] = event | (8 << 16);
	wl_connection_write(client->connection, p, sizeof p);
}

#define WL_DISPLAY_INVALID_OBJECT 0
#define WL_DISPLAY_INVALID_METHOD 1
#define WL_DISPLAY_NO_MEMORY 2

static void
wl_client_connection_data(int fd, uint32_t mask, void *data)
{
	struct wl_client *client = data;
	struct wl_connection *connection = client->connection;
	const struct wl_method *method;
	struct wl_object *object;
	uint32_t p[2], opcode, size;
	uint32_t cmask = 0;
	int len;

	if (mask & WL_EVENT_READABLE)
		cmask |= WL_CONNECTION_READABLE;
	if (mask & WL_EVENT_WRITEABLE)
		cmask |= WL_CONNECTION_WRITABLE;

	len = wl_connection_data(connection, cmask);
	if (len < 0) {
		wl_client_destroy(client);
		return;
	}

	while (len >= sizeof p) {
		wl_connection_copy(connection, p, sizeof p);
		opcode = p[1] & 0xffff;
		size = p[1] >> 16;
		if (len < size)
			break;

		object = wl_hash_lookup(&client->display->objects,
					p[0]);
		if (object == NULL) {
			wl_client_event(client, &client->display->base,
					WL_DISPLAY_INVALID_OBJECT);
			wl_connection_consume(connection, size);
			len -= size;
			continue;
		}
				
		if (opcode >= object->interface->method_count) {
			wl_client_event(client, &client->display->base,
					WL_DISPLAY_INVALID_METHOD);
			wl_connection_consume(connection, size);
			len -= size;
			continue;
		}
				
		method = &object->interface->methods[opcode];
		wl_connection_demarshal_ffi(client->connection,
					    &client->display->objects,
					    FFI_FN(method->func),
					    method->arguments, client, object);

		len -= size;
	}
}

static int
wl_client_connection_update(struct wl_connection *connection,
			    uint32_t mask, void *data)
{
	struct wl_client *client = data;
	uint32_t emask = 0;

	if (mask & WL_CONNECTION_READABLE)
		emask |= WL_EVENT_READABLE;
	if (mask & WL_CONNECTION_WRITABLE)
		emask |= WL_EVENT_WRITEABLE;

	return wl_event_loop_update_source(client->display->loop,
					   client->source, mask);
}

static void
advertise_object(struct wl_client *client, struct wl_object *object)
{
	wl_connection_marshal(client->connection, NULL, object->id, -1, "s",
			      object->interface->name);
}

static void
advertise_objects(struct wl_client *client, struct wl_display *display)
{
	struct wl_object_ref *ref;
	struct wl_list *node;
	uint32_t n;

	node = display->global_objects_list.next;
	for (n = 0; node != &display->global_objects_list; n++) {
		ref = container_of(node, struct wl_object_ref, link);
		node = ref->link.next;
	}

	wl_connection_write(client->connection, &n, sizeof n);
	advertise_object(client, &display->base);

	node = display->global_objects_list.next;
	while (n--) {
		ref = container_of(node, struct wl_object_ref, link);
		advertise_object(client, ref->object);
		node = ref->link.next;
	}

	wl_connection_data(client->connection, WL_CONNECTION_WRITABLE);
}

static struct wl_client *
wl_client_create(struct wl_display *display, int fd)
{
	struct wl_client *client;

	client = malloc(sizeof *client);
	if (client == NULL)
		return NULL;

	memset(client, 0, sizeof *client);
	client->display = display;
	client->source = wl_event_loop_add_fd(display->loop, fd,
					      WL_EVENT_READABLE,
					      wl_client_connection_data, client);
	client->connection = wl_connection_create(fd,
						  wl_client_connection_update, 
						  client);
	wl_list_init(&client->object_list);

	wl_connection_write(client->connection,
			    &display->client_id_range,
			    sizeof display->client_id_range);
	display->client_id_range += 256;

	advertise_objects(client, display);

	wl_list_insert(display->client_list.prev, &client->link);

	return client;
}

void
wl_client_destroy(struct wl_client *client)
{
	struct wl_object_ref *ref;

	printf("disconnect from client %p\n", client);

	wl_list_remove(&client->link);

	while (client->object_list.next != &client->object_list) {
		ref = container_of(client->object_list.next,
				   struct wl_object_ref, link);
		wl_list_remove(&ref->link);
		wl_surface_destroy(client, (struct wl_surface *) ref->object);
		free(ref);
	}

	wl_event_loop_remove_source(client->display->loop, client->source);
	wl_connection_destroy(client->connection);
	free(client);
}

static int
wl_display_create_surface(struct wl_client *client,
			  struct wl_display *display, uint32_t id)
{
	struct wl_surface *surface;
	struct wl_object_ref *ref;

	surface = wl_surface_create(display, id);

	ref = malloc(sizeof *ref);
	if (ref == NULL) {
		wl_client_event(client, &display->base,
				WL_DISPLAY_NO_MEMORY);
		return -1;
	}

	ref->object = &surface->base;
	wl_hash_insert(&display->objects, &surface->base);
	wl_list_insert(client->object_list.prev, &ref->link);

	return 0;
}

static const struct wl_method display_methods[] = {
	WL_DEFMETHOD ("create_surface", "O", wl_display_create_surface)
};

static const struct wl_event display_events[] = {
	WL_DEFEVENT ("invalid_object", "")
	WL_DEFEVENT ("invalid_method", "")
	WL_DEFEVENT ("no_memory", "")
};

static const struct wl_interface display_interface = {
	"display", 1,
	ARRAY_LENGTH(display_methods), display_methods,
	ARRAY_LENGTH(display_events), display_events,
};

WL_EXPORT int
wl_display_register_global_object(struct wl_display *display,
				  struct wl_object *object)
{
	struct wl_object_ref *ref;

	ref = malloc(sizeof *ref);
	if (ref == NULL)
		return -1;

	ref->object = object;
	wl_hash_insert(&display->objects, object);
	wl_list_insert(display->global_objects_list.prev, &ref->link);

	return 0;
}

static void
wl_display_create_backend_advertisement(struct wl_display *display)
{
	struct wl_object *backend_adv;

	backend_adv = wl_backend_advertisement_create(display, 2);
	wl_display_register_global_object (display, backend_adv);
}


WL_EXPORT struct wl_display *
wl_display_create(struct wl_backend *backend, struct wl_compositor *compositor)
{
	struct wl_display *display;

	if (backend == NULL) {
		fprintf(stderr, "failed to retrieve backend\n");
		return NULL;
	}

	if (compositor == NULL) {
		fprintf(stderr, "failed to retrieve compositor\n");
		return NULL;
	}
	display = malloc(sizeof *display);
	if (display == NULL)
		return NULL;

	memset (display, 0, sizeof *display);

	display->loop = wl_event_loop_create();
	if (display->loop == NULL)
		goto fail;

	display->backend = backend;
	display->compositor = compositor;
	display->base.id = 0;
	display->base.interface = &display_interface;
	wl_hash_insert(&display->objects, &display->base);
	wl_list_init(&display->surface_list);
	wl_list_init(&display->client_list);
	wl_list_init(&display->global_objects_list);

	wl_display_create_backend_advertisement(display);

	display->client_id_range = 256; /* Gah, arbitrary... */

	return display;		

fail:
	free(display);
	return NULL;
}

/* TODO: this is inefficient, it marshals data repeatedly!  */

WL_EXPORT void
wl_display_vsend_event(struct wl_display *display, struct wl_object *sender,
		       uint32_t opcode, va_list va)
{
	struct wl_client *client;

	client = container_of(display->client_list.next,
			      struct wl_client, link);
	while (&client->link != &display->client_list) {
		va_list va2;
		va_copy (va2, va);
		wl_connection_vmarshal(client->connection, &display->objects,
				       sender->id, opcode,
				       sender->interface->events[opcode].arguments,
				       va2);

		client = container_of(client->link.next,
				   struct wl_client, link);
	}
}

WL_EXPORT void
wl_display_send_event(struct wl_display *display, struct wl_object *sender,
		      uint32_t opcode, ...)
{
	va_list va;
	va_start (va, opcode);
	wl_display_vsend_event (display, sender, opcode, va);
}

static void
wl_display_destroy(struct wl_display *display)
{
	const struct wl_compositor_interface *interface;

	interface = display->compositor->interface;
	if (interface->notify_display_destroy)
		interface->notify_display_destroy(display->compositor, display);
}

WL_EXPORT struct wl_event_loop *
wl_display_get_event_loop(struct wl_display *display)
{
	return display->loop;
}

WL_EXPORT struct wl_backend *
wl_display_get_backend(struct wl_display *display)
{
	return display->backend;
}

static void
wl_display_run(struct wl_display *display)
{
	signal (SIGTERM, sigterm_handler);
	signal (SIGINT, sigterm_handler);
	display_exit = 0;
	while (!display_exit)
		wl_event_loop_wait(display->loop);
}

/* The plan here is to generate a random anonymous socket name and
 * advertise that through a service on the session dbus.
 */
static const char socket_name[] = "\0wayland";

static void
socket_data(int fd, uint32_t mask, void *data)
{
	struct wl_display *display = data;
	struct sockaddr_un name;
	socklen_t length;
	int client_fd;

	length = sizeof name;
	client_fd = accept (fd, (struct sockaddr *) &name, &length);
	if (client_fd < 0)
		fprintf(stderr, "failed to accept\n");

	wl_client_create(display, client_fd);
}

static int
wl_display_add_socket(struct wl_display *display)
{
	struct sockaddr_un name;
	int sock;
	socklen_t size;

	sock = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (sock < 0)
		return -1;

	name.sun_family = AF_LOCAL;
	memcpy(name.sun_path, socket_name, sizeof socket_name);

	size = offsetof (struct sockaddr_un, sun_path) + sizeof socket_name;
	if (bind(sock, (struct sockaddr *) &name, size) < 0)
		return -1;

	if (listen(sock, 1) < 0)
		return -1;

	wl_event_loop_add_fd(display->loop, sock,
			     WL_EVENT_READABLE,
			     socket_data, display);

	return 0;
}


struct wl_surface_iterator {
	struct wl_list *head;
	struct wl_surface *surface;
	uint32_t mask;
};

WL_EXPORT struct wl_surface_iterator *
wl_surface_iterator_create(struct wl_display *display, uint32_t mask)
{
	struct wl_surface_iterator *iterator;

	iterator = malloc(sizeof *iterator);
	if (iterator == NULL)
		return NULL;

	iterator->head = &display->surface_list;
	iterator->surface = container_of(display->surface_list.next,
					 struct wl_surface, link);
	iterator->mask = mask;

	return iterator;
}

WL_EXPORT int
wl_surface_iterator_next(struct wl_surface_iterator *iterator,
			 struct wl_surface **surface)
{
	if (&iterator->surface->link == iterator->head)
		return 0;

	*surface = iterator->surface;
	iterator->surface = container_of(iterator->surface->link.next,
					 struct wl_surface, link);

	return 1;
}

WL_EXPORT void
wl_surface_iterator_destroy(struct wl_surface_iterator *iterator)
{
	free(iterator);
}

WL_EXPORT struct wl_backend *
wl_backend_create(const char *name, const char *args)
{
	return _wl_backend_create(name, args, 1);
}

static struct wl_display *
load_compositor(int argc, char *argv[])
{
	struct wl_display *(*init)(int, char **);
	struct wl_display *display;
	const char *path = argv[0];
	void *p;

	p = dlopen(path, RTLD_LAZY | RTLD_GLOBAL);
	if (p == NULL) {
		fprintf(stderr, "failed to open compositor %s: %s\n",
			path, dlerror());
		return NULL;
	}

	init = dlsym(p, "wl_compositor_init");
	if (init == NULL) {
		fprintf(stderr, "failed to look up wl_compositor_init\n");
		return NULL;
	}
		
	display = init(argc, argv);
	if (display == NULL) {
		fprintf(stderr, "failed to register display\n");
		return NULL;
	}

	return display;
}

int main(int argc, char *argv[])
{
	struct wl_display *display;
	const char *compositor = "./egl-compositor.so";

	if (argc >= 2)
		compositor = argv[1];
	else
		argv[1] = strdup (compositor);

	display = load_compositor(argc - 1, argv + 1);
	if (wl_display_add_socket(display)) {
		fprintf(stderr, "failed to add socket: %m\n");
		exit(EXIT_FAILURE);
	}
	wl_display_run(display);
	wl_display_destroy(display);
	return 0;
}
