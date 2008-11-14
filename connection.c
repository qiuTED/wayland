#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/uio.h>
#include <ffi.h>
#include <stdarg.h>

#include "connection.h"
#include "hash.h"

#define ARRAY_LENGTH(a) (sizeof (a) / sizeof (a)[0])

struct wl_buffer {
	char data[4096];
	int head, tail;
};

struct wl_connection {
	struct wl_buffer in, out;
	int fd;
	void *data;
	wl_connection_update_func_t update;
};

struct wl_connection *
wl_connection_create(int fd,
		     wl_connection_update_func_t update,
		     void *data)
{
	struct wl_connection *connection;

	connection = malloc(sizeof *connection);
	memset(connection, 0, sizeof *connection);
	connection->fd = fd;
	connection->update = update;
	connection->data = data;

	connection->update(connection,
			   WL_CONNECTION_READABLE,
			   connection->data);

	return connection;
}

void
wl_connection_destroy(struct wl_connection *connection)
{
	free(connection);
}

void
wl_connection_copy(struct wl_connection *connection, void *data, size_t size)
{
	struct wl_buffer *b;
	int tail, rest;

	b = &connection->in;
	tail = b->tail;
	if (tail + size <= ARRAY_LENGTH(b->data)) {
		memcpy(data, b->data + tail, size);
	} else { 
		rest = ARRAY_LENGTH(b->data) - tail;
		memcpy(data, b->data + tail, rest);
		memcpy(data + rest, b->data, size - rest);
	}
}

void
wl_connection_consume(struct wl_connection *connection, size_t size)
{
	struct wl_buffer *b;
	int tail, rest;

	b = &connection->in;
	tail = b->tail;
	if (tail + size <= ARRAY_LENGTH(b->data)) {
		b->tail += size;
	} else { 
		rest = ARRAY_LENGTH(b->data) - tail;
		b->tail = size - rest;
	}
}

int wl_connection_data(struct wl_connection *connection, uint32_t mask)
{
	struct wl_buffer *b;
	struct iovec iov[2];
	int len, head, tail, count, size, available;

	if (mask & WL_CONNECTION_READABLE) {
		b = &connection->in;
		head = connection->in.head;
		if (head < b->tail) {
			iov[0].iov_base = b->data + head;
			iov[0].iov_len = b->tail - head;
			count = 1;
		} else {
			size = ARRAY_LENGTH(b->data) - head;
			iov[0].iov_base = b->data + head;
			iov[0].iov_len = size;
			iov[1].iov_base = b->data;
			iov[1].iov_len = b->tail;
			count = 2;
		}
		len = readv(connection->fd, iov, count);
		if (len < 0) {
			fprintf(stderr,
				"read error from connection %p: %m (%d)\n",
				connection, errno);
			return -1;
		} else if (len == 0) {
			/* FIXME: Handle this better? */
			return -1;
		} else if (head + len <= ARRAY_LENGTH(b->data)) {
			b->head += len;
		} else {
			b->head = head + len - ARRAY_LENGTH(b->data);
		}

		/* We know we have data in the buffer at this point,
		 * so if head equals tail, it means the buffer is
		 * full. */

		available = b->head - b->tail;
		if (available == 0)
			available = sizeof b->data;
		else if (available < 0)
			available += ARRAY_LENGTH(b->data);
	} else {
		available = 0;
	}	

	if (mask & WL_CONNECTION_WRITABLE) {
		b = &connection->out;
		tail = b->tail;
		if (tail < b->head) {
			iov[0].iov_base = b->data + tail;
			iov[0].iov_len = b->head - tail;
			count = 1;
		} else {
			size = ARRAY_LENGTH(b->data) - tail;
			iov[0].iov_base = b->data + tail;
			iov[0].iov_len = size;
			iov[1].iov_base = b->data;
			iov[1].iov_len = b->head;
			count = 2;
		}
		len = writev(connection->fd, iov, count);
		if (len < 0) {
			fprintf(stderr, "write error for connection %p: %m\n", connection);
			return -1;
		} else if (tail + len <= ARRAY_LENGTH(b->data)) {
			b->tail += len;
		} else {
			b->tail = tail + len - ARRAY_LENGTH(b->data);
		}

		/* We just took data out of the buffer, so at this
		 * point if head equals tail, the buffer is empty. */

		if (b->tail == b->head)
			connection->update(connection,
					   WL_CONNECTION_READABLE,
					   connection->data);
	}

	return available;
}

void
wl_connection_write(struct wl_connection *connection, const void *data, size_t count)
{
	struct wl_buffer *b;
	size_t size;
	int head;

	b = &connection->out;
	head = b->head;
	if (head + count <= ARRAY_LENGTH(b->data)) {
		memcpy(b->data + head, data, count);
		b->head += count;
	} else {
		size = ARRAY_LENGTH(b->data) - head;
		memcpy(b->data + head, data, size);
		memcpy(b->data, data + size, count - size);
		b->head = count - size;
	}

	if (b->tail == head)
		connection->update(connection,
				   WL_CONNECTION_READABLE |
				   WL_CONNECTION_WRITABLE,
				   connection->data);
}

static int
strchrcmp (const char **pp, char end_p, const char *q)
{
	const char *p = *pp;
	while (*p != end_p && *p != 0 && *q != 0)
		p++, q++;

	*pp = p;
	return *p == end_p;
}
		

union wl_element {
	uint32_t uint32;
	const char *string;
	void *object;
	uint32_t new_id;
};

void
wl_connection_marshal(struct wl_connection *connection, struct wl_hash *objects,
		      uint32_t obj_id, uint32_t opcode, const char *types, ...)
{
	va_list va;
	va_start (va, types);
	wl_connection_vmarshal(connection, objects, obj_id, opcode, types, va);
}

void
wl_connection_vmarshal(struct wl_connection *connection,
		       struct wl_hash *objects, uint32_t obj_id,
		       uint32_t opcode, const char *types, va_list va)
{
	uint32_t *p;
	int i, id;
	const char *c;
	union wl_element values[20];
	struct wl_object *object;
	uint32_t data[64];
	int size;

	for (i = 0, c = types, size = 2 * sizeof(uint32_t); *c; i++) {
		switch (*c) {
		case 'i':
			values[i].uint32 = va_arg (va, int);
			size += sizeof (uint32_t);
			c++;
			break;
		case 's': {
			const char *s = va_arg (va, const char *);
			int length = strlen (s);
			values[i].string = s;
			size += sizeof (uint32_t);
			size += (length + 3) & ~3;
			c++;
			break;
		}
		case 'o':
			id = va_arg (va, int);
			object = wl_hash_lookup(objects, id);
			if (object == NULL)
				printf("unknown object (%d)\n", id);
			c++;
			values[i].uint32 = id;
			break;
#if 0
		case '{':
			id = va_arg (va, int);
			object = wl_hash_lookup(objects, id);
			if (object == NULL)
				printf("unknown object (%d)\n", id);
			c++;
			if (!strchrcmp (&c, '}', object->interface->name))
				printf("wrong object type\n");
			values[i].uint32 = id;
			break;
#endif
		case 'O':
			values[i].uint32 = id = va_arg (va, int);
			if (objects != NULL) {
				object = wl_hash_lookup(objects, id);
				if (object != NULL)
					printf("object already exists (%d)\n", id);
			}
			size += sizeof (uint32_t);
			c++;
			break;
		default:
			printf("unknown type %c\n", *c++);
			break;
		}
	}

	if (sizeof data < size) {
		printf("request too big, should malloc tmp buffer here\n");
		return;
	}
	data[0] = obj_id;
	data[1] = (size << 16) | (opcode & 65535);
	for (i = 0, c = types, p = &data[2]; *c; i++) {
		switch (*c) {
		case 'i':
		case 'o':
		case 'O':
			*p++ = values[i].uint32;
			c++;
			break;
		case 's': {
			const char *s = values[i].string;
			int length = strlen (s);
			*p++ = length;
			memcpy ((char *)p, s, length);
			p += (length + 3) >> 2;
			c++;
			break;
		}
		case '{':
			*p++ = values[i].uint32;
			c = strchr (c, '}');
			c++;
			break;
		default:
			printf("unknown type %c\n", *c++);
			break;
		}
	}
	wl_connection_write (connection, data, size);
}

int
wl_connection_demarshal_ffi(struct wl_connection *connection,
			    struct wl_hash *objects,
			    void (*func)(void), const char *arguments, ...)
{
	ffi_type *types[20];
	ffi_cif cif;
	uint32_t result, id;
	uint32_t *p;
	int i, size;
	const char *c;
	union wl_element values[20];
	void *args[20];
	struct wl_object *object;
	uint32_t data[64];
	va_list va;

	va_start (va, arguments);
	for (i = 0, c = arguments; *c && *c != '|'; i++) {
		if (i >= ARRAY_LENGTH(types)) {
			printf("too many args (%d)\n", i);
			return -1;
		}

		switch (*c) {
		case 'i':
			types[i] = &ffi_type_uint32;
			values[i].uint32 = va_arg (va, int);
			c++;
			break;
		case 'p':
			types[i] = &ffi_type_pointer;
			values[i].object = va_arg (va, void *);
			c++;
			break;
		case 's':
			types[i] = &ffi_type_pointer;
			values[i].string = va_arg (va, char *);
			values[i].string = strdup (values[i].object);
			c++;
			break;
		case 'o':
			types[i] = &ffi_type_pointer;
			id = va_arg (va, int);
			object = wl_hash_lookup(objects, id);
			if (object == NULL)
				printf("unknown object (%d)\n", id);
			c++;
			values[i].object = object;
			break;
#if 0
		case '{':
			types[i] = &ffi_type_pointer;
			id = va_arg (va, int);
			object = wl_hash_lookup(objects, id);
			if (object == NULL)
				printf("unknown object (%d)\n", id);
			c++;
			if (!strchrcmp (&c, '}', object->interface->name))
				printf("wrong object type\n");
			values[i].object = object;
			break;
#endif
		case 'O':
			types[i] = &ffi_type_uint32;
			values[i].new_id = id = va_arg (va, int);
			if (objects != NULL) {
				object = wl_hash_lookup(objects, id);
				if (object != NULL)
					printf("object already exists (%d)\n", id);
			}
			c++;
			break;
		default:
			printf("unknown type %c\n", *c++);
			break;
		}
		args[i] = &values[i];
	}

	if (*c == '|')
		c++;

	if (connection) {
		wl_connection_copy(connection, data, 2 * sizeof (data[0]));
		id = data[0];
		size = data[1] >> 16;

		if (sizeof data < size) {
			printf("request too big, should malloc tmp buffer here\n");
			return -1;
		}
		wl_connection_copy(connection, data, size);
		wl_connection_consume(connection, size);
	} else
		id = -1, size = 0;
		
	for (p = &data[2]; *c; i++) {
		if ((p - data) * sizeof (data[0]) >= size) {
			printf("incomplete packet\n");
			return -1;
		}
		if (i >= ARRAY_LENGTH(types)) {
			printf("too many args (%d)\n", i);
			return -1;
		}

		switch (*c) {
		case 'i':
			types[i] = &ffi_type_uint32;
			values[i].uint32 = *p;
			p++, c++;
			break;
		case 's': {
			int length = *p++;
			char *s = malloc (length + 1);
			memcpy (s, p, length);
			s[length] = 0;
			p += (length + 3) >> 2, c++;
			types[i] = &ffi_type_pointer;
			values[i].object = s;
			break;
		}
		case 'o':
			types[i] = &ffi_type_pointer;
			object = wl_hash_lookup(objects, *p);
			if (object == NULL)
				printf("unknown object (%d)\n", *p);
			p++, c++;
			values[i].object = object;
			break;
#if 0
		case '{':
			types[i] = &ffi_type_pointer;
			object = wl_hash_lookup(objects, *p);
			if (object == NULL)
				printf("unknown object (%d)\n", *p);
			p++, c++;
			if (!strchrcmp (&c, '}', object->interface->name))
				printf("wrong object type\n");
			values[i].object = object;
			break;
#endif
		case 'O':
			types[i] = &ffi_type_uint32;
			values[i].new_id = *p;
			if (objects != NULL) {
				object = wl_hash_lookup(objects, *p);
				if (object != NULL)
					printf("object already exists (%d)\n", *p);
			}
			p++, c++;
			break;
		default:
			printf("unknown type %c\n", *c++);
			break;
		}
		args[i] = &values[i];
	}

	ffi_prep_cif(&cif, FFI_DEFAULT_ABI, i, &ffi_type_uint32, types);
	ffi_call(&cif, func, &result, args);
	return id;
}
