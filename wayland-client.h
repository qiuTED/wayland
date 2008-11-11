#ifndef _WAYLAND_CLIENT_H
#define _WAYLAND_CLIENT_H

/* GCC visibility */
#if defined(__GNUC__) && __GNUC__ >= 4
#define WL_EXPORT __attribute__ ((visibility("default")))
#else
#define WL_EXPORT
#endif

struct wl_display;
struct wl_surface;
struct wl_buffer;

/* Display functions.  */

#define WL_DISPLAY_READABLE 0x01
#define WL_DISPLAY_WRITABLE 0x02

typedef int (*wl_display_update_func_t)(uint32_t mask, void *data);

struct wl_display *wl_display_create(const char *address);
void wl_display_destroy(struct wl_display *display);
int wl_display_get_fd(struct wl_display *display,
		      wl_display_update_func_t update, void *data);

void wl_display_iterate(struct wl_display *display, uint32_t mask);

typedef void (*wl_display_event_func_t)(struct wl_display *display,
					uint32_t opcode,
					uint32_t arg1, uint32_t arg2,
					void *data);

void wl_display_set_event_handler(struct wl_display *display,
				  wl_display_event_func_t handler,
				  void *data);


struct wl_surface *
wl_display_create_surface(struct wl_display *display);

/* Surface functions.  */

void wl_surface_destroy(struct wl_surface *surface);
void wl_surface_attach(struct wl_surface *surface,
		       uint32_t name, int32_t width, int32_t height, uint32_t stride);
void wl_surface_map(struct wl_surface *surface,
		    int32_t x, int32_t y, int32_t width, int32_t height);
void wl_surface_copy(struct wl_surface *surface, int32_t dst_x, int32_t dst_y,
		     uint32_t name, uint32_t stride,
		     int32_t x, int32_t y, int32_t width, int32_t height);
void wl_surface_damage(struct wl_surface *surface,
		       int32_t x, int32_t y, int32_t width, int32_t height);

void wl_surface_attach_buffer(struct wl_surface *surface,
			      struct wl_buffer *buffer);
void wl_surface_copy_buffer(struct wl_surface *surface,
			    int32_t dst_x, int32_t dst_y, struct wl_buffer *src,
			    int32_t x, int32_t y, int32_t width, int32_t height);

/* Back-end functions.  */

int wl_gem_open (const char *gem_device);

int wl_gem_close (void);

/* Buffer functions.  */

struct wl_buffer {
	int width, height, stride;
	uint32_t name, handle;
};

struct wl_buffer *wl_buffer_create(int width, int height, int stride);

struct wl_buffer *wl_buffer_create_from_data(int width, int height,
					     int stride, void *data);

int wl_buffer_destroy(struct wl_buffer *buffer);

int wl_buffer_data(struct wl_buffer *buffer, void *data);

#endif
