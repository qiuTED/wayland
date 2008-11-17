CFLAGS = -Wall -g -Wstrict-prototypes -Wmissing-prototypes -fvisibility=hidden

PKG_CONFIG_PATH ?= $(HOME)/install/lib/pkgconfig

EAGLE_CFLAGS = $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags eagle)
EAGLE_LDLIBS = $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs eagle)

clients = flower pointer background window
compositors = egl-compositor.so glx-compositor.so

all : wayland libwayland.so $(compositors) $(clients)

wayland_objs =					\
	wayland.o				\
	event-loop.o				\
	connection.o				\
	hash.o					\
	input.o

wayland : LDLIBS += -ldl -rdynamic

wayland : $(wayland_objs)
	gcc -o $@ $(LDLIBS) $(wayland_objs)

libwayland_objs = wayland-backend.o wayland-backend-gem.o wayland-client.o \
	connection.o hash.o

libwayland.so : $(libwayland_objs)

compositors_objs =  $(sort \
	$(foreach c,$(patsubst %.so,%,$(compositors)), $($(c)_objs)))
$(compositors_objs) $(libwayland_objs) : CFLAGS += $(shell pkg-config --cflags libdrm)

$(wayland_objs) $(libwayland_objs) : CFLAGS += $(shell pkg-config --cflags libffi)
wayland libwayland.so : LDLIBS += -lrt $(shell pkg-config --libs libffi)

egl_compositor_objs = egl-compositor.o
$(egl_compositor_objs) : CFLAGS += $(EAGLE_CFLAGS) $(shell pkg-config --cflags libpng)
egl-compositor.so : LDLIBS += $(EAGLE_LDLIBS) $(shell pkg-config --libs libpng) -rdynamic

egl-compositor.so : $(egl_compositor_objs)

glx_compositor_objs = glx-compositor.o
glx-compositor.so : LDLIBS += -lGL

glx-compositor.so : $(glx_compositor_objs)


libwayland.so $(compositors) :
	gcc -o $@ $^ $(LDLIBS) -shared 

flower_objs = flower.o wayland-glib.o cairo-util.o
pointer_objs = pointer.o wayland-glib.o cairo-util.o
background_objs = background.o wayland-glib.o
window_objs = window.o gears.o wayland-glib.o cairo-util.o
clients_objs = $(sort $(foreach c,$(clients), $($(c)_objs)))

$(clients_objs) : CFLAGS += $(shell pkg-config --cflags cairo glib-2.0)
$(clients) : LDLIBS += $(shell pkg-config --libs cairo glib-2.0) -lrt

background.o : CFLAGS += $(shell pkg-config --cflags gdk-pixbuf-2.0)
background : LDLIBS += $(shell pkg-config --libs gdk-pixbuf-2.0)

window.o : CFLAGS += $(EAGLE_CFLAGS)
window : LDLIBS += $(EAGLE_LDLIBS)

define client_template
$(1): $$($(1)_objs) libwayland.so
endef

$(foreach c,$(clients),$(eval $(call client_template,$(c))))

$(clients) :
	gcc -o $@ -L. -lwayland $(LDLIBS) $^

clean :
	rm -f $(clients) wayland *.o *.so
