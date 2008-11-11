#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

#include "wayland-client.h"
#include "wayland-glib.h"

static const char socket_name[] = "\0wayland";

static uint8_t *convert_to_argb(GdkPixbuf *pixbuf, int *new_stride)
{
	uint8_t *data, *row, *src;
	uint32_t *pixel;
	int width, height, stride, channels, i, j;

	width = gdk_pixbuf_get_width(pixbuf);
	height = gdk_pixbuf_get_height(pixbuf);
	stride = gdk_pixbuf_get_rowstride(pixbuf);
	row = gdk_pixbuf_get_pixels(pixbuf);
	channels = gdk_pixbuf_get_n_channels(pixbuf);

	data = malloc(height * width * 4);
	if (data == NULL) {
		fprintf(stderr, "out of memory\n");
		return NULL;
	}

	for (i = 0; i < height; i++) {
		for (j = 0; j < width; j++) {
			src = &row[i * stride + j * channels];
			pixel = (uint32_t *) &data[i * width * 4 + j * 4];
			*pixel = 0xff000000 | (src[0] << 16) | (src[1] << 8) | src[2];
		}
	}

	*new_stride = width * 4;

	return data;
}

static struct wl_buffer *wl_buffer_for_pixbuf(struct wl_display *display,
					      GdkPixbuf *pixbuf)
{
	int32_t width, height, stride;
	void *data;

	width = gdk_pixbuf_get_width(pixbuf);
	height = gdk_pixbuf_get_height(pixbuf);
	data = convert_to_argb(pixbuf, &stride);

	return wl_display_create_buffer_from_data(display, width, height, stride, data);
}

int main(int argc, char *argv[])
{
	GdkPixbuf *image;
	GError *error = NULL;
	struct wl_display *display;
	struct wl_surface *surface;
	struct wl_buffer *buffer;
	GMainLoop *loop;
	GSource *source;

	display = wl_display_create(socket_name);
	if (display == NULL) {
		fprintf(stderr, "failed to create display: %m\n");
		return -1;
	}

	loop = g_main_loop_new(NULL, FALSE);
	source = wayland_source_new(display);
	g_source_attach(source, NULL);

	surface = wl_display_create_surface(display);

	g_type_init();
	image = gdk_pixbuf_new_from_file (argv[1], &error);

	buffer = wl_buffer_for_pixbuf (display, image);
	wl_surface_attach_buffer(surface, buffer);
	wl_surface_map(surface, 0, 0, 1280, 800);

	g_main_loop_run(loop);

	return 0;
}
