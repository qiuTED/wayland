#ifndef _WAYLAND_BACKEND_INTERNAL_H
#define _WAYLAND_BACKEND_INTERNAL_H

/* Backend factories.  */

extern struct wl_backend *wl_gem_open (const char *args);

#endif
