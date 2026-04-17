# libFuzzer harnesses for dmc_unrar. Clang-only — libFuzzer lives inside
# the clang runtime. Invoke from repo root:
#
#   make -C test/fuzz all          # build every harness
#   make -C test/fuzz fuzz_open_mem
#   make -C test/fuzz seed         # copy/symlink corpus+fixtures into seed/
#   make -C test/fuzz clean
#
# See run.sh / minimize.sh for higher-level workflows.

# libFuzzer lives in the clang runtime; force clang here rather than
# relying on `?=` (which loses to make's implicit `CC=cc`). Override
# with `make CC=/path/to/clang` if you need a specific clang build.
CC := clang

HERE    := $(CURDIR)
REPO    := $(CURDIR)/../..
CORPUS  := $(REPO)/test/corpus
FIX     := $(REPO)/test/fixtures

CFLAGS_COMMON  = -std=gnu89 -Wall -Wextra -Wno-long-long -g -O1 \
                 -D_DEFAULT_SOURCE=1
CFLAGS_FUZZ    = $(CFLAGS_COMMON) \
                 -fsanitize=fuzzer,address,undefined \
                 -fno-omit-frame-pointer \
                 -I$(REPO)

TARGETS = fuzz_open_mem fuzz_filename_stat fuzz_extract_mem

.PHONY: all seed clean check-clang

all: check-clang $(TARGETS)

# Fail early (and clearly) if clang isn't in PATH.
check-clang:
	@command -v $(CC) >/dev/null 2>&1 || { \
	  echo "test/fuzz: '$(CC)' not found — libFuzzer requires clang." >&2; \
	  echo "Install clang or set CC=/path/to/clang." >&2; \
	  exit 1; \
	}

fuzz_open_mem: fuzz_open_mem.c $(REPO)/dmc_unrar.c
	$(CC) $(CFLAGS_FUZZ) $< -o $@

fuzz_filename_stat: fuzz_filename_stat.c $(REPO)/dmc_unrar.c
	$(CC) $(CFLAGS_FUZZ) $< -o $@

fuzz_extract_mem: fuzz_extract_mem.c $(REPO)/dmc_unrar.c
	$(CC) $(CFLAGS_FUZZ) $< -o $@

# Populate seed/ from existing corpus + fixtures. Hard links so updates
# flow through, falling back to cp when hard links aren't allowed.
seed:
	@mkdir -p seed
	@for f in $(CORPUS)/*.rar $(FIX)/*.rar; do \
	  [ -f "$$f" ] || continue; \
	  base=$$(basename "$$f"); \
	  dst="seed/$$base"; \
	  [ -e "$$dst" ] && continue; \
	  ln "$$f" "$$dst" 2>/dev/null || cp "$$f" "$$dst"; \
	done
	@echo "seed/ now has $$(ls seed/ 2>/dev/null | wc -l) archive(s)"

clean:
	rm -f $(TARGETS) *.o
	rm -rf work seed
	rm -f crash-* oom-* timeout-* leak-*
