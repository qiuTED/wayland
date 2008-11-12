#ifndef _WAYLAND_EVDEV_H
#define _WAYLAND_EVDEV_H 1

void create_input_devices(struct wl_display *display);

struct wl_object *
wl_input_device_create(struct wl_display *display,
		       const char *path, uint32_t id);

#endif
