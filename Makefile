TARGET=platform# platform or web
EXE = mines

CFLAGS = -Wall -Wextra -pedantic -std=c99
LDFLAGS = -lm

BUILD_MARKER = build/$(TARGET).build
DEP = build/
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEP)/$*.d

ifeq (platform, $(TARGET))
	CC = gcc

	CFLAGS += -ggdb -pipe
	LDFLAGS += -ggdb
	LDFLAGS += -lSDL3
else ifeq (web, $(TARGET))
	CC = emcc
	EXE +=.html

	CFLAGS += -s USE_SDL=3
	LDFLAGS += -s USE_SDL=3
	LDFLAGS += --preload-file directory_or_file
else
	CC ?= false
endif

SRCFILES = main.c

OBJFILES = $(addprefix build/, $(patsubst %.c, %.o, $(SRCFILES)))

$(EXE): $(OBJFILES)
	$(CC) $^ -o $@ $(LDFLAGS)

build/%.o: src/%.c Makefile $(BUILD_MARKER) | build
	$(CC) $(DEPFLAGS) $(CFLAGS) -c $< -o $@

build:
	mkdir build

$(BUILD_MARKER): | build
	rm -fv build/*.build
	touch $(BUILD_MARKER)

.PHONY: clean
clean:
	rm -fv $(OBJFILES)
	rm -frv build
	rm -fv $(EXE)*

DEPFILES := $(OBJFILES:%.o=%.d)
include $(wildcard $(DEPFILES))

