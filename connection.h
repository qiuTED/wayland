#ifndef _CONNECTION_H_
#define _CONNECTION_H_

#include <stdarg.h>

struct wl_connection;
struct wl_hash;

#define WL_CONNECTION_READABLE 0x01
#define WL_CONNECTION_WRITABLE 0x02

typedef int (*wl_connection_update_func_t)(struct wl_connection *connection,
					   uint32_t mask, void *data);

struct wl_connection *wl_connection_create(int fd,
					   wl_connection_update_func_t update,
					   void *data);
void wl_connection_destroy(struct wl_connection *connection);
void wl_connection_copy(struct wl_connection *connection, void *data, size_t size);
void wl_connection_consume(struct wl_connection *connection, size_t size);
int wl_connection_data(struct wl_connection *connection, uint32_t mask);
void wl_connection_write(struct wl_connection *connection, const void *data, size_t count);
int wl_connection_demarshal_ffi(struct wl_connection *connection,
			        struct wl_hash *objects, void (*func)(void),
			        const char *arguments, ...);
int wl_connection_demarshal_mem(struct wl_connection *connection,
				struct wl_hash *objects,
				void *dest, size_t dsize,
				const char *types, ...);
void wl_connection_marshal(struct wl_connection *connection,
			   struct wl_hash *objects, uint32_t id,
			   uint32_t opcode, const char *types, ...);
void wl_connection_vmarshal(struct wl_connection *connection,
			    struct wl_hash *objects, uint32_t obj_id,
			    uint32_t opcode, const char *types, va_list va);

#endif
