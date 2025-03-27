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
LDFLAGS = 

# Debug flags for different test configurations
DEBUG_FLAGS = -DESTALLOC_DEBUG -DESTALLOC_PRINT_DEBUG

# Output directories
OUTDIR = test
LOGDIR = log

# All test configurations
CONFIGS = $(OUTDIR)/test_4_16 \
          $(OUTDIR)/test_4_16_debug \
		  $(OUTDIR)/test_8_16 \
		  $(OUTDIR)/test_8_16_debug \
		  $(OUTDIR)/test_4_24_x86 \
		  $(OUTDIR)/test_4_24_x86_debug \
		  $(OUTDIR)/test_8_24_x86 \
		  $(OUTDIR)/test_8_24_x86_debug \
		  $(OUTDIR)/test_4_24_x64 \
		  $(OUTDIR)/test_4_24_x64_debug \
		  $(OUTDIR)/test_8_24_x64 \
		  $(OUTDIR)/test_8_24_x64_debug

# Source files
SRCS = estalloc.h estalloc.c test.c

.DEFAULT_GOAL := all

# Build all
all: $(CONFIGS)

# Clean everything
clean:
	rm -f *.o
	rm -rf $(OUTDIR)/* $(LOGDIR)/*

# Build rules
$(OUTDIR)/test_4_16: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_32) -DESTALLOC_ALIGNMENT=4 -DESTALLOC_16BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_8_16: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_32) -DESTALLOC_ALIGNMENT=8 -DESTALLOC_16BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_4_16_debug: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_32) $(DEBUG_FLAGS) -DESTALLOC_ALIGNMENT=4 -DESTALLOC_16BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_8_16_debug: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_32) $(DEBUG_FLAGS) -DESTALLOC_ALIGNMENT=8 -DESTALLOC_16BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_4_24_x86: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_32) -DESTALLOC_ALIGNMENT=4 -DESTALLOC_24BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_8_24_x86: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_32) -DESTALLOC_ALIGNMENT=8 -DESTALLOC_24BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_4_24_x86_debug: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_32) $(DEBUG_FLAGS) -DESTALLOC_ALIGNMENT=4 -DESTALLOC_24BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_8_24_x86_debug: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_32) $(DEBUG_FLAGS) -DESTALLOC_ALIGNMENT=8 -DESTALLOC_24BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_4_24_x64: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_64) -DESTALLOC_ALIGNMENT=4 -DESTALLOC_24BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_8_24_x64: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_64) -DESTALLOC_ALIGNMENT=8 -DESTALLOC_24BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_4_24_x64_debug: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_64) $(DEBUG_FLAGS) -DESTALLOC_ALIGNMENT=4 -DESTALLOC_24BIT $^ -o $@ $(LDFLAGS)

$(OUTDIR)/test_8_24_x64_debug: $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS_64) $(DEBUG_FLAGS) -DESTALLOC_ALIGNMENT=8 -DESTALLOC_24BIT $^ -o $@ $(LDFLAGS)

# Run all tests
test: $(CONFIGS)
	@mkdir -p $(LOGDIR)
	@echo "Running all test configurations..."
	@for config in $(CONFIGS); do \
		base=$$(basename $$config); \
		echo "Running $$base..."; \
		./$$config > $(LOGDIR)/$$base.log 2>&1 || echo "$$base FAILED!"; \
	done
	@echo "All tests completed. Check $(LOGDIR)/*.log for results."

.PHONY: all clean test valgrind_test quick_test diff_logs save_expected
