/* libFuzzer harness: exercise the RAR parser / block walker / header
   decoders by running dmc_unrar_archive_open_mem() against arbitrary
   byte buffers. No extraction, no metadata iteration. */

#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#include "../../dmc_unrar.c"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	dmc_unrar_archive a;

	if (dmc_unrar_archive_init(&a) != DMC_UNRAR_OK)
		return 0;

	(void)dmc_unrar_archive_open_mem(&a, data, (dmc_unrar_size_t)size);
	dmc_unrar_archive_close(&a);
	return 0;
}
