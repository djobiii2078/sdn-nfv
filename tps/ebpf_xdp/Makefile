CLANG ?= clang
CC ?= cc
ARCH_INCLUDE ?= /usr/include/$(shell $(CC) -dumpmachine)
BPF_FLAGS = -O2 -g -target bpf -Wall -Wextra -I$(ARCH_INCLUDE)
GOOD = $(patsubst %.bpf.c,build/%.bpf.o,$(wildcard bugs/*.bpf.c solution/*.bpf.c))
GOOD := $(filter-out build/bugs/00_compile.bpf.o,$(GOOD))
all: $(GOOD)
build/%.bpf.o: %.bpf.c include/lab.h include/parser.h
	mkdir -p $(dir $@)
	$(CLANG) $(BPF_FLAGS) -c $< -o $@
compile-bug:
	mkdir -p build/bugs
	$(CLANG) $(BPF_FLAGS) -c bugs/00_compile.bpf.c -o build/bugs/00_compile.bpf.o
native:
	mkdir -p build
	$(CC) -O2 -Wall -Wextra -Wno-unknown-pragmas -DLAB_NATIVE tests/native.c -o build/native
	$(CC) -O2 -Wall -Wextra -Wno-unknown-pragmas -DLAB_NATIVE -DLAB_RAW_PORT tests/native.c -o build/native-endian
	$(CC) -O2 -Wall -Wextra -Wno-unknown-pragmas -DLAB_NATIVE -DLAB_FIXED_IHL tests/native.c -o build/native-ihl
check: native
	python3 tests/cases.py --native build/native
clean:
	rm -rf build
.PHONY: all native check clean compile-bug
