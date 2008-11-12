#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <endian.h>
#include <fcntl.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <GL/gl.h>
#include <GL/glx.h>

#include "wayland.h"
#include "wayland-backend.h"

#define ARRAY_LENGTH(a) (sizeof (a) / sizeof (a)[0])

struct glx_compositor {
	struct wl_compositor base;
	Display *display;
	GLXContext context;
	Window window;
	struct wl_display *wl_display;
	struct wl_backend *backend;
	struct wl_event_source *x_source;
};

struct surface_data {
	GLuint texture;
	GLuint width;
	GLuint height;
	struct wl_map map;
};

static void
repaint(void *data)
{
	struct glx_compositor *gc = data;
	struct wl_surface_iterator *iterator;
	struct wl_surface *surface;
	struct surface_data *sd;
	GLint vertices[12];
	GLint tex_coords[12];
	GLuint indices[4] = { 0, 1, 2, 3 };

	iterator = wl_surface_iterator_create(gc->wl_display, 0);
	while (wl_surface_iterator_next(iterator, &surface)) {
		sd = wl_surface_get_data(surface);
		if (sd == NULL)
			continue;

		vertices[0] = sd->map.x;
		vertices[1] = sd->map.y;
		vertices[2] = 0;
 		tex_coords[0] = 0;
 		tex_coords[1] = 0;

		vertices[3] = sd->map.x;
		vertices[4] = sd->map.y + sd->map.height;
		vertices[5] = 0;
 		tex_coords[2] = 0;
 		tex_coords[3] = sd->height;

		vertices[6] = sd->map.x + sd->map.width;
		vertices[7] = sd->map.y;
		vertices[8] = 0;
 		tex_coords[4] = sd->width;
 		tex_coords[5] = 0;

		vertices[9] = sd->map.x + sd->map.width;
		vertices[10] = sd->map.y + sd->map.height;
		vertices[11] = 0;
 		tex_coords[6] = sd->width;
 		tex_coords[7] = sd->height;

		glBindTexture(GL_TEXTURE_RECTANGLE_ARB, sd->texture);
		glEnable(GL_TEXTURE_RECTANGLE_ARB);
		glEnable(GL_BLEND);
		/* Assume pre-multiplied alpha for now, this probably
		 * needs to be a wayland visual type of thing. */
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glVertexPointer(3, GL_INT, 0, vertices);
		glTexCoordPointer(2, GL_INT, 0, tex_coords);
		glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_INT, indices);
	}
	wl_surface_iterator_destroy(iterator);

	glXSwapBuffers(gc->display, gc->window);
}

static void
schedule_repaint(struct glx_compositor *gc)
{
	struct wl_event_loop *loop;

	loop = wl_display_get_event_loop(gc->wl_display);
	wl_event_loop_add_idle(loop, repaint, gc);
}

static void
notify_surface_create(struct wl_compositor *compositor,
		      struct wl_surface *surface)
{
	struct surface_data *sd;

	sd = malloc(sizeof *sd);
	if (sd == NULL)
		return;

	wl_surface_set_data(surface, sd);

	glGenTextures(1, &sd->texture);
}
				   
static void
notify_surface_destroy(struct wl_compositor *compositor,
		       struct wl_surface *surface)
{
	struct glx_compositor *gc = (struct glx_compositor *) compositor;
	struct surface_data *sd;

	sd = wl_surface_get_data(surface);
	if (sd == NULL)
		return;

	glDeleteTextures(1, &sd->texture);

	free(sd);

	schedule_repaint(gc);
}

static void
notify_surface_attach(struct wl_compositor *compositor,
		      struct wl_surface *surface, uint32_t name, 
		      uint32_t width, uint32_t height,
		      uint32_t stride)
{
	struct glx_compositor *gc = (struct glx_compositor *) compositor;
	struct wl_backend *backend;
	struct surface_data *sd;
	struct wl_buffer *b;
	void *data;

	backend = wl_display_get_backend (gc->wl_display);
	sd = wl_surface_get_data(surface);
	if (sd == NULL)
		return;

	sd->width = width;
	sd->height = height;
	b = wl_backend_open_buffer (backend, width, height, stride, name);
	data = wl_buffer_get_data (b);
	if (data == NULL) {
                if (errno == ENOMEM)
			fprintf(stderr, "malloc for gem_pread failed\n");
		else
			fprintf(stderr, "gem_pread failed");
		return;
	}

	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, sd->texture);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glTexParameterf(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_R, GL_REPEAT);
	glTexParameterf(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#if __BYTE_ORDER == __LITTLE_ENDIAN
	glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height, 0,
		     GL_BGRA, GL_UNSIGNED_BYTE, data);
#else
	glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height, 0,
		     GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, data);
#endif

	wl_buffer_free_data(b, data);
	wl_buffer_destroy (b);

	schedule_repaint(gc);
}

static void
notify_surface_map(struct wl_compositor *compositor,
		   struct wl_surface *surface, struct wl_map *map)
{
	struct glx_compositor *gc = (struct glx_compositor *) compositor;
	struct surface_data *sd;

	sd = wl_surface_get_data(surface);
	if (sd == NULL)
		return;

	sd->map = *map;

	schedule_repaint(gc);
}

static void
notify_surface_copy(struct wl_compositor *compositor,
		    struct wl_surface *surface,
		    int32_t dst_x, int32_t dst_y,
		    uint32_t name, uint32_t stride,
		    int32_t x, int32_t y, int32_t width, int32_t height)
{
}

static void
notify_surface_damage(struct wl_compositor *compositor,
		      struct wl_surface *surface,
		      int32_t x, int32_t y, int32_t width, int32_t height)
{
	struct glx_compositor *gc = (struct glx_compositor *) compositor;

	schedule_repaint(gc);
}


static const struct wl_compositor_interface interface = {
	notify_surface_create,
	notify_surface_destroy,
	notify_surface_attach,
	notify_surface_map,
	notify_surface_copy,
	notify_surface_damage
};

static void
display_data(int fd, uint32_t mask, void *data)
{
	struct glx_compositor *gc = data;
	XEvent ev;

	while (XPending(gc->display) > 0) {
		XNextEvent(gc->display, &ev);
		/* Some day we'll do something useful with these events. */
	}
}

WL_EXPORT struct wl_display *
wl_compositor_init(int argc, char **argv)
{
	static int attribs[] = {
		GLX_RGBA,
		GLX_RED_SIZE, 1,
		GLX_GREEN_SIZE, 1,
		GLX_BLUE_SIZE, 1,
		GLX_DOUBLEBUFFER,
		GLX_DEPTH_SIZE, 1,
		None
	};
	struct glx_compositor *gc;
	const int x = 100, y = 100, width = 1024, height = 768;
	XSetWindowAttributes attr;
	unsigned long mask;
	Window root;
	XVisualInfo *visinfo;
	int screen;
	struct wl_event_loop *loop;
	struct wl_backend *backend;

	gc = malloc(sizeof *gc);
	if (gc == NULL)
		return NULL;

	gc->base.interface = &interface;
	backend = wl_backend_create("shm", NULL);
	gc->wl_display = wl_display_create(backend, &gc->base);
	if (gc->wl_display == NULL) {
		wl_backend_destroy(backend);
		return NULL;
	}

	gc->display = XOpenDisplay(NULL);
	if (gc->display == NULL) {
		free(gc);
		return NULL;
	}

	loop = wl_display_get_event_loop(gc->wl_display);
	gc->x_source = wl_event_loop_add_fd(loop,
					    ConnectionNumber(gc->display),
					    WL_EVENT_READABLE,
					    display_data, gc);

	screen = DefaultScreen(gc->display);
	root = RootWindow(gc->display, screen);

	visinfo = glXChooseVisual(gc->display, screen, attribs);

	/* window attributes */
	attr.background_pixel = 0;
	attr.border_pixel = 0;
	attr.colormap = XCreateColormap(gc->display,
					root, visinfo->visual, AllocNone);
	attr.event_mask = StructureNotifyMask | ExposureMask | KeyPressMask;
	mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;
	gc->window = XCreateWindow(gc->display, root, x, y, width, height,
				   0, visinfo->depth, InputOutput,
				   visinfo->visual, mask, &attr);

	gc->context = glXCreateContext(gc->display, visinfo, NULL, True);

	XMapWindow(gc->display, gc->window);
	glXMakeCurrent(gc->display, gc->window, gc->context);

	glViewport(0, 0, width, height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, width, height, 0, 0, 1000.0);
	glMatrixMode(GL_MODELVIEW);
	glClearColor(0.0, 0.05, 0.2, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);

	schedule_repaint(gc);

	return gc->wl_display;
}
