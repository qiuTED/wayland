#ifndef _HASH_H_
#define _HASH_H_


struct wl_hash {
	struct wl_object **objects;
	uint32_t count, alloc, id;
};

int wl_hash_insert(struct wl_hash *hash, struct wl_object *object);
struct wl_object *wl_hash_lookup(struct wl_hash *hash, uint32_t id);
void wl_hash_delete(struct wl_hash *hash, struct wl_object *object);

#endif
