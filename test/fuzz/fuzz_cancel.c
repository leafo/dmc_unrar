/* libFuzzer harness: exercise the cancellation hook across open, list, and
   extract. Cancel returns false after a fuzzer-chosen number of polls.
   Follow-on calls on the same archive verify DMC_UNRAR_USER_CANCEL unwinds
   cleanly and leaves no dangling allocator / solid-chain state.
   ASan catches leaks and use-after-free from the cancel path. */

#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../dmc_unrar.c"

#define FUZZ_MAX_EXTRACT_SIZE ((uint64_t)(16 * 1024 * 1024))

typedef struct {
	int budget;   /* number of poll calls before we return false; -1 = never cancel. */
	int calls;
} cancel_state;

static bool cancel_after_budget(void *opaque) {
	cancel_state *cs = (cancel_state *)opaque;
	cs->calls++;
	if (cs->budget < 0)
		return true;
	return cs->calls <= cs->budget;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	dmc_unrar_archive a;
	cancel_state cs;
	int budget_open, budget_extract;

	if (size < 2)
		return 0;

	/* Derive per-call cancel budgets from the first couple of bytes.
	   Keeps fuzzer-minimized inputs reproducible. Budget 255 means
	   "effectively no cancel" for most archives. */
	budget_open    = (int)data[0];
	budget_extract = (int)data[1];

	if (dmc_unrar_archive_init(&a) != DMC_UNRAR_OK)
		return 0;

	/* Cancel during open (block-collect walk). */
	cs.budget = budget_open;
	cs.calls  = 0;
	a.cancel.func   = cancel_after_budget;
	a.cancel.opaque = &cs;

	if (dmc_unrar_archive_open_mem(&a, data, (dmc_unrar_size_t)size) != DMC_UNRAR_OK) {
		dmc_unrar_archive_close(&a);
		return 0;
	}

	/* Iterate metadata + extract each supported entry, with a fresh cancel
	   budget for the extract phase. This exercises the cancel poll points
	   inside the block-collect loops, solid predecessor skip, and main
	   decompressor loops. */
	{
		dmc_unrar_size_t count = dmc_unrar_get_file_count(&a), i;

		cs.budget = budget_extract;
		cs.calls  = 0;

		for (i = 0; i < count; i++) {
			const dmc_unrar_file *stat;
			void *out = NULL;
			dmc_unrar_size_t out_size = 0;

			(void)dmc_unrar_file_is_directory(&a, i);
			if (dmc_unrar_file_is_supported(&a, i) != DMC_UNRAR_OK)
				continue;
			stat = dmc_unrar_get_file_stat(&a, i);
			if (!stat || stat->uncompressed_size > FUZZ_MAX_EXTRACT_SIZE)
				continue;

			(void)dmc_unrar_extract_file_to_heap(&a, i, &out, &out_size, true);
			free(out);
		}
	}

	/* Disable cancel and re-extract the first supported entry. If the
	   cancel path earlier corrupted the archive or leaked decoder state,
	   ASan / UBSan catches it here. */
	a.cancel.func   = NULL;
	a.cancel.opaque = NULL;
	{
		dmc_unrar_size_t count = dmc_unrar_get_file_count(&a), i;
		for (i = 0; i < count; i++) {
			const dmc_unrar_file *stat;
			void *out = NULL;
			dmc_unrar_size_t out_size = 0;

			if (dmc_unrar_file_is_directory(&a, i))
				continue;
			if (dmc_unrar_file_is_supported(&a, i) != DMC_UNRAR_OK)
				continue;
			stat = dmc_unrar_get_file_stat(&a, i);
			if (!stat || stat->uncompressed_size > FUZZ_MAX_EXTRACT_SIZE)
				continue;
			(void)dmc_unrar_extract_file_to_heap(&a, i, &out, &out_size, true);
			free(out);
			break;
		}
	}

	dmc_unrar_archive_close(&a);
	return 0;
}
