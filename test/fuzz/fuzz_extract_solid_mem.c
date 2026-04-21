/* libFuzzer harness: stress extraction state reuse by extracting a later
   supported file first, then extracting multiple supported files in order.
   This is targeted at solid archives, where later files depend on decoder
   state produced by earlier files. */

#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../dmc_unrar.c"

/* Keep this target focused on state-machine bugs, not resource exhaustion. */
#define FUZZ_MAX_EXTRACT_SIZE ((uint64_t)(1024 * 1024))
#define FUZZ_MAX_TOTAL_EXTRACT_SIZE ((uint64_t)(8 * 1024 * 1024))
#define FUZZ_MAX_FILES ((dmc_unrar_size_t)64)

static bool fuzz_entry_is_small_supported(dmc_unrar_archive *a,
		dmc_unrar_size_t index, uint64_t *size_out) {
	const dmc_unrar_file *stat;

	if (dmc_unrar_file_is_directory(a, index))
		return false;
	if (dmc_unrar_file_is_supported(a, index) != DMC_UNRAR_OK)
		return false;

	stat = dmc_unrar_get_file_stat(a, index);
	if (!stat)
		return false;
	if (stat->uncompressed_size > FUZZ_MAX_EXTRACT_SIZE)
		return false;

	*size_out = stat->uncompressed_size;
	return true;
}

static void fuzz_extract_one(dmc_unrar_archive *a, dmc_unrar_size_t index) {
	void *out = NULL;
	dmc_unrar_size_t out_size = 0;

	(void)dmc_unrar_extract_file_to_heap(a, index, &out, &out_size, true);
	free(out);
}

static void fuzz_extract_last_supported(dmc_unrar_archive *a) {
	dmc_unrar_size_t count = dmc_unrar_get_file_count(a);
	dmc_unrar_size_t i;

	for (i = count; i > 0; i--) {
		dmc_unrar_size_t index = i - 1;
		uint64_t size = 0;

		if (!fuzz_entry_is_small_supported(a, index, &size))
			continue;

		(void)size;
		fuzz_extract_one(a, index);
		break;
	}
}

static void fuzz_extract_sequential(dmc_unrar_archive *a) {
	dmc_unrar_size_t count = dmc_unrar_get_file_count(a);
	dmc_unrar_size_t limit = count < FUZZ_MAX_FILES ? count : FUZZ_MAX_FILES;
	dmc_unrar_size_t i;
	uint64_t total = 0;

	for (i = 0; i < limit; i++) {
		uint64_t size = 0;

		if (!fuzz_entry_is_small_supported(a, i, &size))
			continue;
		if (total > FUZZ_MAX_TOTAL_EXTRACT_SIZE - size)
			break;

		fuzz_extract_one(a, i);
		total += size;
	}
}

static void fuzz_open_and_extract(const uint8_t *data, size_t size, bool last_only) {
	dmc_unrar_archive a;

	if (dmc_unrar_archive_init(&a) != DMC_UNRAR_OK)
		return;

	if (dmc_unrar_archive_open_mem(&a, data, (dmc_unrar_size_t)size) == DMC_UNRAR_OK) {
		if (last_only)
			fuzz_extract_last_supported(&a);
		else
			fuzz_extract_sequential(&a);
	}

	dmc_unrar_archive_close(&a);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	fuzz_open_and_extract(data, size, true);
	fuzz_open_and_extract(data, size, false);
	return 0;
}
