#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <cairo.h>
#include <glib.h>

#include "cairo-util.h"
#include "wayland-client.h"
#include "wayland-glib.h"

static const char gem_device[] = "/dev/dri/card0";
static const char socket_name[] = "\0wayland";

static void
set_random_color(cairo_t *cr)
{
	cairo_set_source_rgba(cr,
			      0.5 + (random() % 50) / 49.0,
			      0.5 + (random() % 50) / 49.0,
			      0.5 + (random() % 50) / 49.0,
			      0.5 + (random() % 100) / 99.0);
}


static void *
draw_stuff(int width, int height)
{
	const int petal_count = 3 + random() % 5;
	const double r1 = 60 + random() % 35;
	const double r2 = 20 + random() % 40;
	const double u = (10 + random() % 90) / 100.0;
	const double v = (random() % 90) / 100.0;

	cairo_surface_t *surface;
	cairo_t *cr;
	int i;
	double t, dt = 2 * M_PI / (petal_count * 2);
	double x1, y1, x2, y2, x3, y3;

	surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
					     width, height);

	cr = cairo_create(surface);
	cairo_translate(cr, width / 2, height / 2);
	cairo_move_to(cr, cos(t) * r1, sin(t) * r1);
	for (t = 0, i = 0; i < petal_count; i++, t += dt * 2) {
		x1 = cos(t) * r1;
		y1 = sin(t) * r1;
		x2 = cos(t + dt) * r2;
		y2 = sin(t + dt) * r2;
		x3 = cos(t + 2 * dt) * r1;
		y3 = sin(t + 2 * dt) * r1;

		cairo_curve_to(cr,
			       x1 - y1 * u, y1 + x1 * u,
			       x2 + y2 * v, y2 - x2 * v,
			       x2, y2);			       

		cairo_curve_to(cr,
			       x2 - y2 * v, y2 + x2 * v,
			       x3 + y3 * u, y3 - x3 * u,
			       x3, y3);
	}

	cairo_close_path(cr);
	set_random_color(cr);
	cairo_fill_preserve(cr);
	set_random_color(cr);
	cairo_stroke(cr);

	cairo_destroy(cr);

	return surface;
}

struct flower {
	struct wl_surface *surface;
	struct wl_buffer *buffer;
	int i;
	int x, y, width, height;
};

static gboolean
move_flower(gpointer data)
{
	struct flower *flower = data;

	wl_surface_map(flower->surface, 
		       flower->x + cos(flower->i / 31.0) * 400 - flower->width / 2,
		       flower->y + sin(flower->i / 27.0) * 300 - flower->height / 2,
		       flower->width, flower->height);
	flower->i++;

	return TRUE;
}

int main(int argc, char *argv[])
{
	struct wl_display *display;
	cairo_surface_t *s;
	struct timespec ts;
	GMainLoop *loop;
	GSource *source;
	struct flower flower;

	if (wl_gem_open (gem_device) < 0) {
		fprintf(stderr, "drm open failed: %m\n");
		return -1;
	}

	loop = g_main_loop_new(NULL, FALSE);

	display = wl_display_create(socket_name);
	if (display == NULL) {
		fprintf(stderr, "failed to create display: %m\n");
		return -1;
	}

	source = wayland_source_new(display);
	g_source_attach(source, NULL);

	flower.x = 512;
	flower.y = 384;
	flower.width = 200;
	flower.height = 200;
	flower.surface = wl_display_create_surface(display);

	clock_gettime(CLOCK_MONOTONIC, &ts);
	srandom(ts.tv_nsec);
	flower.i = ts.tv_nsec;

	s = draw_stuff(flower.width, flower.height);
	flower.buffer = wl_buffer_create_from_cairo_surface(s);

	wl_surface_attach_buffer(flower.surface, flower.buffer);

	g_timeout_add(20, move_flower, &flower);

	g_main_loop_run(loop);

	return 0;
}
