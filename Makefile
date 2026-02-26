CC ?= cc
CFLAGS = -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
         -Wno-sign-compare -Wno-missing-field-initializers \
         -fno-strict-aliasing -g -O2 -I.
LDFLAGS =

# sljit source
SLJIT_SRC = sljit_src/sljitLir.c

SRCS = c2sljit.c c2sljit-driver.c $(SLJIT_SRC)
OBJS = c2sljit.o c2sljit-driver.o sljitLir.o
TARGET = c2sljit

# dlsym needs -ldl on Linux
UNAME_S := $(shell uname -s)
ifneq ($(UNAME_S),Darwin)
  LDFLAGS += -ldl
endif

# MIR project (for bench target)
MIR_DIR = $(HOME)/Projects/mir

.PHONY: all clean test test-integration

all: $(TARGET)

# Build sljit as a separate object
sljitLir.o: $(SLJIT_SRC)
	$(CC) $(CFLAGS) -DSLJIT_CONFIG_AUTO=1 -c -o $@ $<

c2sljit.o: c2sljit.c c2sljit.h mir-compat.h mir-alloc.h mir-varr.h mir-htab.h mir-dlist.h mir-hash.h
	$(CC) $(CFLAGS) -DSLJIT_CONFIG_AUTO=1 -c -o $@ $<

c2sljit-driver.o: c2sljit-driver.c c2sljit.h mir-compat.h mir-alloc.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ -lm

test: $(TARGET)
	@echo "=== Test 1: return 42 ==="
	@echo 'int main() { return 42; }' > /tmp/test1.c
	@./$(TARGET) /tmp/test1.c; echo "Exit code: $$?"
	@echo ""
	@echo "=== Test 2: arithmetic ==="
	@echo 'int main() { int x = 10 + 32; return x; }' > /tmp/test2.c
	@./$(TARGET) /tmp/test2.c; echo "Exit code: $$?"
	@echo ""
	@echo "=== Test 3: if/else ==="
	@echo 'int main() { int x = 5; if (x > 3) return 1; return 0; }' > /tmp/test3.c
	@./$(TARGET) /tmp/test3.c; echo "Exit code: $$?"
	@echo ""
	@echo "=== Test 4: for loop ==="
	@echo 'int main() { int s = 0; for (int i = 0; i < 10; i++) s += i; return s; }' > /tmp/test4.c
	@./$(TARGET) /tmp/test4.c; echo "Exit code: $$?"

test-integration: $(TARGET)
	@pass=0; fail=0; \
	for t in tests/*.c; do \
	  name=$$(basename $$t .c); \
	  for opt in "" "-O1"; do \
	    mode=$${opt:-O0}; \
	    ./$(TARGET) $$opt $$t > /tmp/c2sljit-$$name-$$mode.out 2>/dev/null; \
	    if diff -q tests/$$name.expected /tmp/c2sljit-$$name-$$mode.out >/dev/null 2>&1; then \
	      echo "PASS: $$name $$mode"; pass=$$((pass+1)); \
	    else echo "FAIL: $$name $$mode"; fail=$$((fail+1)); fi; \
	  done; \
	done; echo "$$pass passed, $$fail failed"

# Benchmark: c2sljit vs c2mir
# c2sljit.o and libmir.a both contain the c2mir frontend, which has
# duplicate symbols (debug_node, get_int_basic_type).  We create a
# copy of c2sljit.o with only the c2sljit_* API symbols kept global.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  BENCH_LDFLAGS =
else
  BENCH_LDFLAGS = -ldl
endif

bench.o: bench.c
	$(CC) $(CFLAGS) -I$(MIR_DIR) -I$(MIR_DIR)/c2mir -c -o $@ $<

bench-c2sljit.o: c2sljit.o
	cp $< $@
	printf '_c2sljit_init\n_c2sljit_finish\n_c2sljit_compile\n_c2sljit_get_main\n' > /tmp/bench-exports.txt
	nmedit -s /tmp/bench-exports.txt $@

bench: bench.o bench-c2sljit.o sljitLir.o $(MIR_DIR)/libmir.a
	$(CC) $(LDFLAGS) $(BENCH_LDFLAGS) -o $@ $^ -lm

clean:
	rm -f $(TARGET) $(OBJS) bench bench.o bench-c2sljit.o /tmp/test[1-9].c /tmp/bench-exports.txt
