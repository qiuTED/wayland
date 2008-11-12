#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "wayland-client.h"
#include "wayland-backend.h"
#include "wayland-backend-internal.h"

/* FIXME: Move this to an appropriate Autoconf macro.  */

/* For various ports, try to guess a fixed spot in the vm space
   that's probably free; taken from GCC.  */
#if defined(__alpha)
# define DATA_BASE     0x10000000000
#elif defined(__ia64)
# define DATA_BASE     0x2000000100000000
#elif defined(__x86_64)
# define DATA_BASE     0x1000000000
#elif defined(__arm)
# define DATA_BASE     0x60000000
#elif defined(__i386)
# define DATA_BASE     0x60000000
#elif defined(__powerpc__)
# define DATA_BASE     0x60000000
#elif defined(__s390x__)
# define DATA_BASE     0x8000000000
#elif defined(__s390__)
# define DATA_BASE     0x60000000
#elif defined(__sparc__) && defined(__LP64__)
# define DATA_BASE     0x8000000000
#elif defined(__sparc__)
# define DATA_BASE     0x60000000
#else
# define DATA_BASE     0
#endif


/* Manage memory with Doug Lea's malloc.  */

#define USE_LOCKS 1
#define USE_DL_PREFIX 1
#define USE_BUILTIN_FFS 1

/* We only use the segment created my create_mspace_with_base.  */
#define MSPACES 1
#define ONLY_MSPACES 1

#define HAVE_MORECORE 0
#define HAVE_MMAP 1
#define HAVE_MREMAP 0

/* We have no use for this, so save some code and data.  */
#define NO_MALLINFO 1

/* We need all allocations to be in regular segments.  */
#define DEFAULT_MMAP_THRESHOLD MAX_SIZE_T

/* Don't allocate more than a page unless needed.  */
#define DEFAULT_GRANULARITY ((size_t)malloc_getpagesize)

#define mmap(a, b, c, d, e, f)	MFAIL
#define munmap(a, b)		(-1)
#include "dlmalloc.c"
#undef mmap
#undef munmap


struct wl_backend_private {
        struct wl_backend public;
        struct wl_backend_shared *shared;
	int fd;
	int server;
};

struct wl_handle {
	void *data;

	/* If BASE == NULL, points to next free handle.
	   If BASE != NULL, incremented by open and decremented by close.  */
	long refcount;
};

struct wl_backend_shared {
	struct wl_handle handles[1020];
	mspace msp;
	char *base;
	size_t size;
};

#define HANDLE_TABLE_SIZE	(1024 * sizeof (struct wl_handle))
#define DATA_SIZE		(64 << 20)

#define NUM_HANDLES(backend)	(sizeof ((backend)->shared->handles) / \
				 sizeof ((backend)->shared->handles[0]))
#define HANDLE(backend, n)	(&(backend)->shared->handles[(n)])
#define NEXTFREE(backend, n)	((backend)->shared->handles[(n)].refcount)

static int
wl_shm_destroy (struct wl_backend *b)
{
        struct wl_backend_private *backend = (struct wl_backend_private *) b;
        int rc;
	munmap (backend->shared->base, backend->shared->size);
	munmap (backend->shared,  HANDLE_TABLE_SIZE);
        rc = close (backend->fd);
        if (backend->server)
		shm_unlink (backend->public.args);
	free (backend);
        return rc;
}

/* FIXME: These are not thread-safe yet.  */

static struct wl_buffer *
wl_shm_buffer_open(struct wl_backend *b, int width, int height, int stride,
		   int name)
{
	struct wl_buffer *buffer;
	struct wl_backend_private *backend = (struct wl_backend_private *) b;
	struct wl_handle *h = HANDLE (backend, name);

	if (h->data == NULL)
		return NULL;

        buffer = malloc(sizeof *buffer);
	buffer->backend = b;
        buffer->width = width;
        buffer->height = height;
        buffer->stride = stride;
	buffer->name = name;
	h->refcount++;

	return buffer;
}

static struct wl_buffer *
wl_shm_buffer_create(struct wl_backend *b, int width, int height, int stride)
{
	struct wl_buffer *buffer;
	struct wl_backend_private *backend = (struct wl_backend_private *) b;
	int name = NEXTFREE(backend, 0);
	struct wl_handle *h;

	if (name == -1) {
		fprintf(stderr, "shm alloc failed: no more resources\n");
		return NULL;
	}

	buffer = malloc(sizeof *buffer);
	buffer->backend = b;
	buffer->width = width;
	buffer->height = height;
	buffer->stride = stride;
	buffer->name = name;

	h = HANDLE (backend, name);
	h->data = mspace_malloc (backend->shared->msp, height * stride);
	if (h->data == NULL) {
		fprintf(stderr, "shm alloc failed: %m\n");
		free(buffer);
		return NULL;
	}

	if (NEXTFREE(backend, name) == 0)
		NEXTFREE(backend, 0)++;
	else
		NEXTFREE(backend, 0) = NEXTFREE (backend, name);
	h->refcount = 1;
	return buffer;
}

static int
wl_shm_buffer_destroy(struct wl_buffer *buffer)
{
	struct wl_backend_private *backend = (struct wl_backend_private *) buffer->backend;
	struct wl_handle *h = HANDLE (backend, buffer->name);

	if (h->data == NULL)
		return -1;

	if (--h->refcount == 0) {
		mspace_free (backend->shared->msp, h->data);
		h->data = NULL;
		NEXTFREE(backend, buffer->name) = NEXTFREE(backend, 0);
		NEXTFREE(backend, 0) = buffer->name;
	}
	
	free (buffer);
	return 0;
}

static void *
wl_shm_buffer_get_data(struct wl_buffer *buffer)
{
	struct wl_backend_private *backend = (struct wl_backend_private *) buffer->backend;
	struct wl_handle *h = HANDLE (backend, buffer->name);
	return h->data;
}

static int
wl_shm_buffer_free_data(struct wl_buffer *buffer, void *data)
{
	return 0;
}

static int
wl_shm_buffer_set_data(struct wl_buffer *buffer, void *data)
{
	struct wl_backend_private *backend = (struct wl_backend_private *) buffer->backend;
	struct wl_handle *h = HANDLE (backend, buffer->name);

	if (h->data == NULL)
		return -1;

	memcpy (h->data, data, buffer->height * buffer->stride);
	return 0;
}

struct wl_backend *
wl_shm_open (const char *args, int server)
{
	struct wl_backend_private *backend;
        int fd;

	if (!args)
		args = "/shmem";

	if (server)
		fd = shm_open(args, O_RDWR | O_EXCL | O_CREAT, 0777);
	else
		fd = shm_open(args, O_RDWR, 0);
	if (fd == -1)
		return NULL;

	if (server) {
		if (ftruncate (fd, HANDLE_TABLE_SIZE + DATA_SIZE) == -1)
			goto fail;
	}

	backend = malloc (sizeof *backend);
	backend->fd = fd;
	backend->server = server;

	backend->shared = mmap (NULL, HANDLE_TABLE_SIZE,
			      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (backend->shared == NULL)
		goto fail;

	if (server) {
		NEXTFREE(backend, NUM_HANDLES(backend) - 1) = -1;
		NEXTFREE(backend, 0) = 1;
		backend->shared->base = mmap ((void *) DATA_BASE, DATA_SIZE,
					    PROT_READ | PROT_WRITE,
					    MAP_SHARED | MAP_NORESERVE,
					    fd, HANDLE_TABLE_SIZE);
		if (backend->shared->base == NULL)
			goto fail;

		backend->shared->size = DATA_SIZE;
		backend->shared->msp =
		  create_mspace_with_base (backend->shared->base,
					   backend->shared->size, 0);
	} else {
		if (mmap (backend->shared->base, backend->shared->size,
			  PROT_READ | PROT_WRITE,
			  MAP_FIXED | MAP_SHARED | MAP_NORESERVE, fd,
			  HANDLE_TABLE_SIZE)
		    != backend->shared->base)
			goto fail;
	}

	backend->public.backend_name = "shm";
	backend->public.args = strdup (args);
	backend->public.destroy = wl_shm_destroy;
	backend->public.buffer_create = wl_shm_buffer_create;
	backend->public.buffer_open = wl_shm_buffer_open;
	backend->public.buffer_get_data = wl_shm_buffer_get_data;
	backend->public.buffer_set_data = wl_shm_buffer_set_data;
	backend->public.buffer_free_data = wl_shm_buffer_free_data;
	backend->public.buffer_destroy = wl_shm_buffer_destroy;
	return &backend->public;

fail:
	if (backend->shared)
		munmap (backend->shared,  HANDLE_TABLE_SIZE);
	free (backend);
	if (fd != -1) {
		close (fd);
		if (server)
			shm_unlink (args);
	}
	return NULL;
}
