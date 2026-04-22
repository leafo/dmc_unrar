/* libFuzzer harness: exercise dmc_unrar_archive_open() via a caller-
   supplied IO handler (read/seek callbacks) instead of the built-in
   memory reader. Matches the shape wharfd uses -- its Go binding wraps
   an io.ReaderAt in a read/seek pair, so any bug specific to that code
   path (opaque handling, short reads, SEEK_CUR/END paths, caller-seek
   failure propagation) won't be surfaced by fuzz_open_mem. */

#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../../dmc_unrar.c"

typedef struct {
	const uint8_t   *data;
	dmc_unrar_size_t size;
	dmc_unrar_size_t offset;
} cb_io;

static void *cb_open(const char *path) { (void)path; return NULL; }
static void  cb_close(void *opaque) { (void)opaque; }

static dmc_unrar_size_t cb_read(void *opaque, void *buffer, dmc_unrar_size_t n) {
	cb_io *io = (cb_io *)opaque;
	dmc_unrar_size_t avail = (io->offset < io->size) ? (io->size - io->offset) : 0;
	dmc_unrar_size_t take  = (n < avail) ? n : avail;
	if (take > 0)
		memcpy(buffer, io->data + io->offset, take);
	io->offset += take;
	return take;
}

static bool cb_seek(void *opaque, dmc_unrar_offset_t offset, int origin) {
	cb_io *io = (cb_io *)opaque;
	dmc_unrar_offset_t target;

	if (origin == DMC_UNRAR_SEEK_SET) {
		target = offset;
	} else if (origin == DMC_UNRAR_SEEK_CUR) {
		target = (dmc_unrar_offset_t)io->offset + offset;
	} else if (origin == DMC_UNRAR_SEEK_END) {
		target = (dmc_unrar_offset_t)io->size + offset;
	} else {
		return false;
	}

	if (target < 0 || (dmc_unrar_size_t)target > io->size)
		return false;

	io->offset = (dmc_unrar_size_t)target;
	return true;
}

static dmc_unrar_offset_t cb_tell(void *opaque) {
	cb_io *io = (cb_io *)opaque;
	return (dmc_unrar_offset_t)io->offset;
}

static dmc_unrar_io_handler cb_handler = {
	cb_open, cb_close, cb_read, cb_seek, cb_tell
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	dmc_unrar_archive a;
	cb_io io;

	io.data   = data;
	io.size   = (dmc_unrar_size_t)size;
	io.offset = 0;

	if (dmc_unrar_archive_init(&a) != DMC_UNRAR_OK)
		return 0;

	/* Wire callbacks into the archive's io. dmc_unrar_io_init populates
	   archive->io.size by seeking to end via cb_seek / cb_tell; without it
	   dmc_unrar_archive_open short-circuits on io.size == 0. This matches
	   what the _mem / _file / _path variants do internally. */
	if (!dmc_unrar_io_init(&a.io, &cb_handler, &io)) {
		dmc_unrar_archive_close(&a);
		return 0;
	}

	(void)dmc_unrar_archive_open(&a);
	dmc_unrar_archive_close(&a);
	return 0;
}
