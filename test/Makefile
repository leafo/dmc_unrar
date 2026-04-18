# Test harness for dmc_unrar. Run from the repo root:
#
#   make -C test test           # plain build, run tests
#   make -C test test-asan      # address + UB sanitizer
#   make -C test test-ubsan     # UB sanitizer only
#   make -C test test-caps      # archive-bomb cap smoke tests
#   make -C test diff           # differential extract vs system unrar
#   make -C test corpus         # regenerate test/corpus/*.rar (needs `rar`)
#   make -C test c89            # warnings-only compile of dmc_unrar.c
#   make -C test clean

CC      ?= gcc
CFLAGS_COMMON  = -std=gnu89 -Wall -Wextra -Wno-long-long -g -O1
CFLAGS_DEBUG   = $(CFLAGS_COMMON) -D_FORTIFY_SOURCE=0
CFLAGS_ASAN    = $(CFLAGS_COMMON) -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS_UBSAN   = $(CFLAGS_COMMON) -fsanitize=undefined -fno-omit-frame-pointer

# Tests #include "../dmc_unrar.c" directly, so we only compile runner.c.
SRC = runner.c

TESTROOT = $(CURDIR)
REPO     = $(CURDIR)/..

.PHONY: all test test-asan test-ubsan test-caps diff corpus fixtures c89 clean

all: test

# Plain build + run.
test: runner
	cd $(REPO) && $(TESTROOT)/runner

runner: $(SRC) $(REPO)/dmc_unrar.c
	$(CC) $(CFLAGS_DEBUG) -I$(REPO) $(SRC) -o $@

# Sanitizer builds.
test-asan: runner-asan
	cd $(REPO) && ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	  $(TESTROOT)/runner-asan

runner-asan: $(SRC) $(REPO)/dmc_unrar.c
	$(CC) $(CFLAGS_ASAN) -I$(REPO) $(SRC) -o $@

test-ubsan: runner-ubsan
	cd $(REPO) && UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	  $(TESTROOT)/runner-ubsan

runner-ubsan: $(SRC) $(REPO)/dmc_unrar.c
	$(CC) $(CFLAGS_UBSAN) -I$(REPO) $(SRC) -o $@

# Archive-bomb cap smoke tests. Each binary is cap_test.c compiled with
# one DMC_UNRAR_MAX_* cap forced low enough to reject simple.rar at open
# time. Passing means the library returned DMC_UNRAR_INVALID_DATA.
test-caps: cap_test_file_count cap_test_file_size cap_test_total_size
	cd $(REPO) && $(TESTROOT)/cap_test_file_count
	cd $(REPO) && $(TESTROOT)/cap_test_file_size
	cd $(REPO) && $(TESTROOT)/cap_test_total_size

cap_test_file_count: cap_test.c $(REPO)/dmc_unrar.c
	$(CC) $(CFLAGS_DEBUG) -I$(REPO) -DDMC_UNRAR_MAX_FILE_COUNT=1 cap_test.c -o $@

cap_test_file_size: cap_test.c $(REPO)/dmc_unrar.c
	$(CC) $(CFLAGS_DEBUG) -I$(REPO) -DDMC_UNRAR_MAX_FILE_SIZE=1 cap_test.c -o $@

cap_test_total_size: cap_test.c $(REPO)/dmc_unrar.c
	$(CC) $(CFLAGS_DEBUG) -I$(REPO) -DDMC_UNRAR_MAX_TOTAL_SIZE=1 cap_test.c -o $@

# Differential extraction vs system unrar.
diff:
	cd $(REPO) && $(TESTROOT)/diff.sh

# Regenerate corpus (requires `rar`).
corpus:
	cd $(REPO) && $(TESTROOT)/corpus/build.sh
	cd $(REPO) && $(TESTROOT)/fixtures/build_fixtures.sh

# Just fixtures (needs corpus to already exist).
fixtures:
	cd $(REPO) && $(TESTROOT)/fixtures/build_fixtures.sh

# Strict C89 compile check of the library itself (no link).
c89:
	$(CC) -std=c89 -Wall -Wextra -pedantic -Wno-long-long \
	  -DDMC_UNRAR_DISABLE_BE32TOH_BE64TOH=1 \
	  -c $(REPO)/dmc_unrar.c -o /dev/null

clean:
	rm -f runner runner-asan runner-ubsan \
	      cap_test_file_count cap_test_file_size cap_test_total_size
