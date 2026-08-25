##############################################################
#  Makefile for ESTALLOC library.
#
#  Original Copyright:
#    (C) 2025- HASUMI Hitoshi @hasumikin
#
#  This file is distributed under BSD 3-Clause License.
##############################################################

CC = gcc
CFLAGS_64 = -Wall -Wextra -g -O0
CFLAGS_32 = -Wall -Wextra -g -O0 -m32
# arm64_32 (Apple Watch) is AArch64 with 4-byte pointers. Forcing
# PLATFORM_64BIT on an ILP32 build reproduces its struct layout, including the
# pool header whose sizeof is not a multiple of 8. See issue #2.
CFLAGS_ILP32_P64 = -Wall -Wextra -g -O0 -m32 -DPLATFORM_64BIT
LDFLAGS =

# Debug flags for different test configurations
DEBUG_FLAGS = -DESTALLOC_DEBUG -DESTALLOC_PRINT_DEBUG

# Output directories
OUTDIR = build
LOGDIR = log

# All test configurations
CONFIGS = $(OUTDIR)/test_4_16_32bit \
          $(OUTDIR)/test_4_16_32bit_debug \
		  $(OUTDIR)/test_8_16_32bit \
		  $(OUTDIR)/test_8_16_32bit_debug \
		  $(OUTDIR)/test_4_24_32bit \
		  $(OUTDIR)/test_4_24_32bit_debug \
		  $(OUTDIR)/test_8_24_32bit \
		  $(OUTDIR)/test_8_24_32bit_debug \
		  $(OUTDIR)/test_4_24_64bit \
		  $(OUTDIR)/test_4_24_64bit_debug \
		  $(OUTDIR)/test_8_24_64bit \
		  $(OUTDIR)/test_8_24_64bit_debug \
		  $(OUTDIR)/test_4_24_arm64_32 \
		  $(OUTDIR)/test_4_24_arm64_32_debug \
		  $(OUTDIR)/test_8_24_arm64_32 \
		  $(OUTDIR)/test_8_24_arm64_32_debug

# Source files
SRCS = estalloc.h estalloc.c test/test.c

.DEFAULT_GOAL := all

# Build all
all: $(CONFIGS)

# Clean everything
clean:
	rm -f *.o
	rm -rf $(OUTDIR)/* $(LOGDIR)/*

# Build rules
$(OUTDIR)/test_4_16_32bit: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_32) -DESTALLOC_ALIGNMENT=4 -DESTALLOC_ADDRESS_16BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_8_16_32bit: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_32) -DESTALLOC_ALIGNMENT=8 -DESTALLOC_ADDRESS_16BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_4_16_32bit_debug: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_32) $(DEBUG_FLAGS) -DESTALLOC_ALIGNMENT=4 -DESTALLOC_ADDRESS_16BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_8_16_32bit_debug: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_32) $(DEBUG_FLAGS) -DESTALLOC_ALIGNMENT=8 -DESTALLOC_ADDRESS_16BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_4_24_32bit: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_32) -DESTALLOC_ALIGNMENT=4 -DESTALLOC_ADDRESS_24BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_8_24_32bit: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_32) -DESTALLOC_ALIGNMENT=8 -DESTALLOC_ADDRESS_24BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_4_24_32bit_debug: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_32) $(DEBUG_FLAGS) -DESTALLOC_ALIGNMENT=4 -DESTALLOC_ADDRESS_24BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_8_24_32bit_debug: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_32) $(DEBUG_FLAGS) -DESTALLOC_ALIGNMENT=8 -DESTALLOC_ADDRESS_24BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_4_24_64bit: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_64) -DESTALLOC_ALIGNMENT=4 -DESTALLOC_ADDRESS_24BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_8_24_64bit: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_64) -DESTALLOC_ALIGNMENT=8 -DESTALLOC_ADDRESS_24BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_4_24_64bit_debug: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_64) $(DEBUG_FLAGS) -DESTALLOC_ALIGNMENT=4 -DESTALLOC_ADDRESS_24BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_8_24_64bit_debug: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_64) $(DEBUG_FLAGS) -DESTALLOC_ALIGNMENT=8 -DESTALLOC_ADDRESS_24BIT $^ -o $@ $(LDFLAGS)

# ILP32 with PLATFORM_64BIT forced: reproduces the arm64_32 layout of issue #2.
# ESTALLOC_ADDRESS_16BIT is rejected by estalloc.h in this combination.
$(OUTDIR)/test_4_24_arm64_32: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_ILP32_P64) -DESTALLOC_ALIGNMENT=4 -DESTALLOC_ADDRESS_24BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_8_24_arm64_32: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_ILP32_P64) -DESTALLOC_ALIGNMENT=8 -DESTALLOC_ADDRESS_24BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_4_24_arm64_32_debug: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_ILP32_P64) $(DEBUG_FLAGS) -DESTALLOC_ALIGNMENT=4 -DESTALLOC_ADDRESS_24BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_8_24_arm64_32_debug: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_ILP32_P64) $(DEBUG_FLAGS) -DESTALLOC_ALIGNMENT=8 -DESTALLOC_ADDRESS_24BIT $^ -o $@ $(LDFLAGS)

# Run all tests
test: $(CONFIGS)
	@mkdir -p $(LOGDIR)
	@echo "Running all test configurations..."
	@failed=0; \
	for config in $(CONFIGS); do \
		base=$$(basename $$config); \
		echo "Running $$base..."; \
		if ! ./$$config > $(LOGDIR)/$$base.log 2>&1; then \
			echo "$$base FAILED!"; \
			failed=1; \
		fi; \
	done; \
	if [ $$failed -ne 0 ]; then \
		echo "Some tests failed. Check $(LOGDIR)/*.log for results."; \
		exit 1; \
	fi
	@echo "All tests completed. Check $(LOGDIR)/*.log for results."

.PHONY: all clean test valgrind_test quick_test diff_logs save_expected
