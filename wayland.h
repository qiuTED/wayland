#ifndef WAYLAND_H
#define WAYLAND_H

#include <stdint.h>
#include <stdarg.h>

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

struct wl_client;
struct wl_compositor;

enum {
	WL_ARGUMENT_UINT32 = 'i',
	WL_ARGUMENT_STRING = 's',
	WL_ARGUMENT_POINTER = 'p',
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

#define WL_DEFMETHOD(name, args, func) {name, func, "pp|" args },
#define WL_DEFEVENT(name, args) {name, args},

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

struct wl_backend *wl_backend_create(const char *name, const char *args);

struct wl_event_loop *wl_display_get_event_loop(struct wl_display *display);
void wl_display_vsend_event(struct wl_display *display, struct wl_object *sender,
			    uint32_t event, va_list va);
void wl_display_send_event(struct wl_display *display, struct wl_object *sender,
			   uint32_t event, ...);
struct wl_backend *wl_display_get_backend(struct wl_display *display);
int wl_display_register_global_object(struct wl_display *display,
				      struct wl_object *object);

void wl_surface_set_data(struct wl_surface *surface, void *data);
void *wl_surface_get_data(struct wl_surface *surface);

struct wl_surface_iterator;
struct wl_surface_iterator *
wl_surface_iterator_create(struct wl_display *display, uint32_t mask);
int wl_surface_iterator_next(struct wl_surface_iterator *iterator,
			     struct wl_surface **surface);
void wl_surface_iterator_destroy(struct wl_surface_iterator *iterator);

struct wl_display *
wl_display_create(struct wl_backend *backend, struct wl_compositor *compositor);

struct wl_object *
wl_backend_advertisement_create(struct wl_display *display, uint32_t id);

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

	void (*notify_display_destroy)(struct wl_compositor *compositor,
				       struct wl_display *display);
};

struct wl_display *wl_compositor_init(int argc, char **argv);

#endif
