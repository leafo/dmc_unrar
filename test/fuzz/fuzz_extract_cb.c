/* libFuzzer harness: exercise dmc_unrar_extract_file_with_callback via a
   caller-supplied IO handler AND a caller-supplied extract callback. This
   is the closest possible match to wharfd's actual runtime shape: its Go
   binding opens through read/seek callbacks and extracts through a
   per-chunk callback into a savior sink. Bugs along the callback error
   propagation, short-fill, and buffer-swap paths won't be found by
   fuzz_extract_mem (which uses _extract_to_heap). */

#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../dmc_unrar.c"

/* Cap per-entry declared size so one malformed header can't tank the fuzzer
   with a legitimate multi-GiB allocation. */
#define FUZZ_MAX_EXTRACT_SIZE ((uint64_t)(16 * 1024 * 1024))

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
	if (origin == DMC_UNRAR_SEEK_SET) target = offset;
	else if (origin == DMC_UNRAR_SEEK_CUR) target = (dmc_unrar_offset_t)io->offset + offset;
	else if (origin == DMC_UNRAR_SEEK_END) target = (dmc_unrar_offset_t)io->size + offset;
	else return false;
	if (target < 0 || (dmc_unrar_size_t)target > io->size) return false;
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

/* Per-call state for the extract callback. Uses a PRNG seeded from the
   first bytes of the fuzz input to drive decisions deterministically for
   a given input. */
typedef struct {
	uint32_t        rng;
	uint32_t        call_count;
	dmc_unrar_size_t total;
} extract_cb_state;

static uint32_t rng_next(uint32_t *s) {
	*s = (*s) * 1664525u + 1013904223u;
	return *s;
}

/* Callback vary behaviour:
    - ~6% of calls: return false with err=DMC_UNRAR_OK to request cancel.
    - ~6% of calls: return false with a specific error code, verifies that
      the library propagates the err field faithfully.
    - otherwise: return true and accept the bytes. */
static bool extract_cb(void *opaque, void **buffer, dmc_unrar_size_t *buffer_size,
                       dmc_unrar_size_t uncompressed_size, dmc_unrar_return *err) {
	extract_cb_state *st = (extract_cb_state *)opaque;
	uint32_t roll;

	(void)buffer;

	st->call_count++;
	st->total += uncompressed_size;

	roll = rng_next(&st->rng) & 0xFFu;

	if (roll < 16) {
		*err = DMC_UNRAR_OK;
		return false;
	}
	if (roll < 32) {
		*err = DMC_UNRAR_INVALID_DATA;
		return false;
	}

	/* Occasionally shrink the buffer_size to exercise the short-fill loop
	   in file_extract_with_callback_and_extractor. */
	if ((roll & 0x03u) == 0 && *buffer_size > 32) {
		*buffer_size = *buffer_size / 2;
	}

	return true;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	dmc_unrar_archive a;
	cb_io io;
	dmc_unrar_size_t count, i;
	extract_cb_state cb_state;

	io.data   = data;
	io.size   = (dmc_unrar_size_t)size;
	io.offset = 0;

	/* Seed the callback's PRNG from the first 4 bytes of the input so
	   fuzzer-minimized crashes reproduce deterministically. */
	cb_state.rng = 0x12345678u;
	if (size >= 4) {
		cb_state.rng ^= (uint32_t)data[0] |
		                ((uint32_t)data[1] << 8) |
		                ((uint32_t)data[2] << 16) |
		                ((uint32_t)data[3] << 24);
	}
	cb_state.call_count = 0;
	cb_state.total      = 0;

	if (dmc_unrar_archive_init(&a) != DMC_UNRAR_OK)
		return 0;

	/* dmc_unrar_io_init seeds io.size from cb_seek/cb_tell; without it
	   archive_open short-circuits on io.size == 0. */
	if (!dmc_unrar_io_init(&a.io, &cb_handler, &io)) {
		dmc_unrar_archive_close(&a);
		return 0;
	}

	if (dmc_unrar_archive_open(&a) != DMC_UNRAR_OK) {
		dmc_unrar_archive_close(&a);
		return 0;
	}

	count = dmc_unrar_get_file_count(&a);
	for (i = 0; i < count; i++) {
		const dmc_unrar_file *stat;

		if (dmc_unrar_file_is_directory(&a, i))
			continue;
		if (dmc_unrar_file_is_supported(&a, i) != DMC_UNRAR_OK)
			continue;
		stat = dmc_unrar_get_file_stat(&a, i);
		if (!stat)
			continue;
		if (stat->uncompressed_size > FUZZ_MAX_EXTRACT_SIZE)
			continue;

		(void)dmc_unrar_extract_file_with_callback(&a, i, NULL, 4096, NULL,
		                                           true, &cb_state, extract_cb);
	}

	dmc_unrar_archive_close(&a);
	return 0;
}
