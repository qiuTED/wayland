#include <stdio.h>
#include <stdlib.h>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>

#include <linux/input.h>

#include "wayland.h"
#include "evdev.h"

struct wl_input_device {
	struct wl_object base;
	struct wl_event_source *source;
	struct wl_display *display;
	int fd;
	int tool;
	int32_t x, y;
};

static const struct wl_event input_device_events[] = {
	WL_DEFEVENT ("motion", "ii")
	WL_DEFEVENT ("button", "ii")
};

static const struct wl_interface input_device_interface = {
	"input_device", 1,
	0, NULL,
	ARRAY_LENGTH(input_device_events),
	input_device_events,
};

#define WL_POINTER_MOTION 0
#define WL_POINTER_BUTTON 1

static void
wl_input_device_post_motion_event(struct wl_input_device *device, int x, int y)
{
	wl_display_send_event(device->display, &device->base,
			      WL_POINTER_MOTION, x, y);
}


static void
wl_input_device_post_button_event(struct wl_input_device *device,
				  int button, int state)
{
	wl_display_send_event(device->display, &device->base,
			      WL_POINTER_BUTTON, button, state);
}


static void wl_input_device_data(int fd, uint32_t mask, void *data)
{
	struct wl_input_device *device = data;
	struct input_event ev[8], *e, *end;
	int len, value, dx, dy, new_x, new_y, absolute_event;

	new_x = 0;
	new_y = 0;
	dx = 0;
	dy = 0;
	absolute_event = 0;

	len = read(fd, &ev, sizeof ev);
	if (len < 0 || len % sizeof e[0] != 0) {
		/* FIXME: handle error... reopen device? */;
		return;
	}

	e = ev;
	end = (void *) ev + len;
	for (e = ev; e < end; e++) {
		/* Get the signed value, earlier kernels had this as unsigned */
		value = e->value;

		switch (e->type) {
		case EV_REL:
			switch (e->code) {
			case REL_X:
				dx += value;
				break;

			case REL_Y:
				dy += value;
				break;
			}

		case EV_ABS:
		        absolute_event = 1;
			switch (e->code) {
			case ABS_X:
				new_x = value;
				break;
			case ABS_Y:
				new_y = value;
				break;
			}

		case EV_KEY:
			if (value == 2)
				break;

			switch (e->code) {
			case BTN_TOUCH:
			case BTN_TOOL_PEN:
			case BTN_TOOL_RUBBER:
			case BTN_TOOL_BRUSH:
			case BTN_TOOL_PENCIL:
			case BTN_TOOL_AIRBRUSH:
			case BTN_TOOL_FINGER:
			case BTN_TOOL_MOUSE:
			case BTN_TOOL_LENS:
				device->tool = value ? e->code : 0;
				break;

			case BTN_LEFT:
				wl_input_device_post_button_event(device, 0, value);
				break;

			case BTN_RIGHT:
				wl_input_device_post_button_event(device, 2, value);
				break;

			case BTN_MIDDLE:
				wl_input_device_post_button_event(device, 1, value);
				break;
			}
		}
	}

	if (dx != 0 || dy != 0) {
		device->x += dx;	
		device->y += dy;	
		wl_input_device_post_motion_event(device, device->x, device->y);
	}
	if (absolute_event && device->tool) {
		device->x = new_x;	
		device->y = new_y;	
		wl_input_device_post_motion_event(device, device->x, device->y);
	}
}

struct wl_object *
wl_input_device_create(struct wl_display *display,
		       const char *path, uint32_t id)
{
	struct wl_input_device *device;
	struct wl_event_loop *loop;

	device = malloc(sizeof *device);
	if (device == NULL)
		return NULL;

	device->base.id = id;
	device->base.interface = &input_device_interface;
	device->display = display;
	device->tool = 1;

	device->fd = open(path, O_RDONLY);
	if (device->fd < 0) {
		free(device);
		fprintf(stderr, "couldn't create pointer for %s: %m\n", path);
		return NULL;
	}

	loop = wl_display_get_event_loop(display);
	device->source = wl_event_loop_add_fd(loop, device->fd,
					      WL_EVENT_READABLE,
					      wl_input_device_data, device);
	if (device->source == NULL) {
		close(device->fd);
		free(device);
		return NULL;
	}

	return &device->base;
}

static const char input_device_file[] = 
	"/dev/input/by-id/usb-Apple__Inc._Apple_Internal_Keyboard_._Trackpad-event-mouse";

void
create_input_devices(struct wl_display *display)
{
	struct wl_object *pointer;
	const char *path;

	path = getenv("WAYLAND_POINTER");
	if (path == NULL)
		path = input_device_file;

	pointer = wl_input_device_create(display, path, 1);
	if (pointer)
		wl_display_register_global_object (display, pointer);
}
