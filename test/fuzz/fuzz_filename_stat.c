/* libFuzzer harness: exercise the metadata / filename decoders by
   iterating every entry after opening. Reads filenames (both the
   size-probe and fill call), stats, directory and support checks. */

#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../dmc_unrar.c"

/* Refuse obviously-nonsense name sizes to keep the fuzzer from spending
   all its time allocating multi-gigabyte buffers for made-up lengths
   read out of a malformed header. A real RAR filename is capped by the
   format at 64 KiB. */
#define FUZZ_MAX_NAME 65536

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	dmc_unrar_archive a;
	dmc_unrar_size_t count, i;

	if (dmc_unrar_archive_init(&a) != DMC_UNRAR_OK)
		return 0;

	if (dmc_unrar_archive_open_mem(&a, data, (dmc_unrar_size_t)size) != DMC_UNRAR_OK) {
		dmc_unrar_archive_close(&a);
		return 0;
	}

	count = dmc_unrar_get_file_count(&a);
	for (i = 0; i < count; i++) {
		dmc_unrar_size_t need;
		char *name;

		(void)dmc_unrar_get_file_stat(&a, i);
		(void)dmc_unrar_file_is_directory(&a, i);
		(void)dmc_unrar_file_is_supported(&a, i);

		need = dmc_unrar_get_filename(&a, i, NULL, 0);
		if (need == 0 || need > FUZZ_MAX_NAME)
			continue;

		name = (char *)malloc(need);
		if (!name)
			continue;
		(void)dmc_unrar_get_filename(&a, i, name, need);
		free(name);
	}

	dmc_unrar_archive_close(&a);
	return 0;
}
