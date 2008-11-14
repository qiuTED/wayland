#ifndef WAYLAND_H
#define WAYLAND_H

#include <stdint.h>

/* GCC visibility */
#if defined(__GNUC__) && __GNUC__ >= 4
#define WL_EXPORT __attribute__ ((visibility("default")))
#else
#define WL_EXPORT
#endif

#define ARRAY_LENGTH(a) (sizeof (a) / sizeof (a)[0])

#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})

struct wl_list {
	struct wl_list *prev;
	struct wl_list *next;
};

void wl_list_init(struct wl_list *list);
void wl_list_insert(struct wl_list *list, struct wl_list *elm);
void wl_list_remove(struct wl_list *elm);

enum {
	WL_EVENT_READABLE = 0x01,
	WL_EVENT_WRITEABLE = 0x02
};

struct wl_event_loop;
struct wl_event_source;
typedef void (*wl_event_loop_fd_func_t)(int fd, uint32_t mask, void *data);
typedef void (*wl_event_loop_idle_func_t)(void *data);

struct wl_event_loop *wl_event_loop_create(void);
void wl_event_loop_destroy(struct wl_event_loop *loop);
struct wl_event_source *wl_event_loop_add_fd(struct wl_event_loop *loop,
					     int fd, uint32_t mask,
					     wl_event_loop_fd_func_t func,
					     void *data);
int wl_event_loop_update_source(struct wl_event_loop *loop,
				struct wl_event_source *source,
				uint32_t mask);

int wl_event_loop_remove_source(struct wl_event_loop *loop,
				struct wl_event_source *source);
int wl_event_loop_wait(struct wl_event_loop *loop);
struct wl_event_source *wl_event_loop_add_idle(struct wl_event_loop *loop,
					       wl_event_loop_idle_func_t func,
					       void *data);

struct wl_hash {
	struct wl_object **objects;
	uint32_t count, alloc, id;
};

int wl_hash_insert(struct wl_hash *hash, struct wl_object *object);
struct wl_object *wl_hash_lookup(struct wl_hash *hash, uint32_t id);
void wl_hash_delete(struct wl_hash *hash, struct wl_object *object);

struct wl_client;

enum {
	WL_ARGUMENT_UINT32 = 'i',
	WL_ARGUMENT_STRING = 's',
	WL_ARGUMENT_OBJECT = 'o',
	WL_ARGUMENT_INTERFACE = '{',
	WL_ARGUMENT_NEW_ID = 'O'
};

struct wl_method {
	const char *name;
	void *func;
	const char *arguments;
};

struct wl_event {
	const char *name;
	const char *arguments;
};

struct wl_interface {
	const char *name;
	int version;
	int method_count;
	const struct wl_method *methods;
	int event_count;
	const struct wl_event *events;
};

struct wl_object {
	const struct wl_interface *interface;
	uint32_t id;
};

struct wl_surface;
struct wl_display;

struct wl_map {
	int32_t x, y, width, height;
};

struct wl_event_loop *wl_display_get_event_loop(struct wl_display *display);

void wl_surface_set_data(struct wl_surface *surface, void *data);
void *wl_surface_get_data(struct wl_surface *surface);

struct wl_surface_iterator;
struct wl_surface_iterator *
wl_surface_iterator_create(struct wl_display *display, uint32_t mask);
int wl_surface_iterator_next(struct wl_surface_iterator *iterator,
			     struct wl_surface **surface);
void wl_surface_iterator_destroy(struct wl_surface_iterator *iterator);

struct wl_object *
wl_input_device_create(struct wl_display *display,
		       const char *path, uint32_t id);
void
wl_display_post_relative_event(struct wl_display *display,
			       struct wl_object *source, int dx, int dy);
void
wl_display_post_absolute_event(struct wl_display *display,
			       struct wl_object *source, int x, int y);
void
wl_display_post_button_event(struct wl_display *display,
			     struct wl_object *source, int button, int state);

struct wl_compositor {
	const struct wl_compositor_interface *interface;
};

struct wl_compositor_interface {
	void (*notify_surface_create)(struct wl_compositor *compositor,
				      struct wl_surface *surface);
	void (*notify_surface_destroy)(struct wl_compositor *compositor,
				       struct wl_surface *surface);
	void (*notify_surface_attach)(struct wl_compositor *compositor,
				      struct wl_surface *surface,
				      uint32_t name, 
				      uint32_t width, uint32_t height,
				      uint32_t stride);
	void (*notify_surface_map)(struct wl_compositor *compositor,
				   struct wl_surface *surface,
				   struct wl_map *map);
	void (*notify_surface_copy)(struct wl_compositor *compositor,
				    struct wl_surface *surface,
				    int32_t dst_x, int32_t dst_y,
				    uint32_t name, uint32_t stride,
				    int32_t x, int32_t y,
				    int32_t width, int32_t height);
	void (*notify_surface_damage)(struct wl_compositor *compositor,
				      struct wl_surface *surface,
				      int32_t x, int32_t y,
				      int32_t width, int32_t height);
};

void wl_display_set_compositor(struct wl_display *display,
			       struct wl_compositor *compositor);

struct wl_compositor *
wl_compositor_create(struct wl_display *display);

#endif
