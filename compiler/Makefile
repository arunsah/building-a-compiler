CC ?= cc
BUILD_DIR ?= build

CPPFLAGS := -Iinclude
BASE_CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
	-Wformat=2 -Wno-format-nonliteral -Werror -O0 -g3
CFLAGS ?= $(BASE_CFLAGS)
LDFLAGS ?=

LIB_SOURCES := src/diagnostic.c src/source.c src/token.c src/lexer.c \
	src/syntax.c src/parser.c src/checked.c src/checker.c src/ir.c \
	src/ir_verify.c src/lower.c src/interpreter.c src/x86_64_assembly.c \
	src/system_tools.c src/build.c
HEADERS := include/study/build.h include/study/checked.h include/study/checker.h \
	include/study/diagnostic.h include/study/lexer.h include/study/location.h \
	include/study/interpreter.h include/study/ir.h include/study/ir_verify.h \
	include/study/lower.h include/study/parser.h include/study/source.h \
	include/study/syntax.h include/study/system_tools.h include/study/token.h \
	include/study/x86_64_assembly.h

.PHONY: all test sanitize clean

all: $(BUILD_DIR)/studycc

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/studycc: src/main.c $(LIB_SOURCES) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) src/main.c $(LIB_SOURCES) -o $@

$(BUILD_DIR)/source-test: tests/source_test.c $(LIB_SOURCES) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) tests/source_test.c $(LIB_SOURCES) -o $@

$(BUILD_DIR)/lexer-test: tests/lexer_test.c $(LIB_SOURCES) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) tests/lexer_test.c $(LIB_SOURCES) -o $@

$(BUILD_DIR)/parser-test: tests/parser_test.c $(LIB_SOURCES) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) tests/parser_test.c $(LIB_SOURCES) -o $@

$(BUILD_DIR)/checker-test: tests/checker_test.c $(LIB_SOURCES) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) tests/checker_test.c $(LIB_SOURCES) -o $@

$(BUILD_DIR)/ir-test: tests/ir_test.c $(LIB_SOURCES) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) tests/ir_test.c $(LIB_SOURCES) -o $@

test: $(BUILD_DIR)/studycc $(BUILD_DIR)/source-test $(BUILD_DIR)/lexer-test \
		$(BUILD_DIR)/parser-test $(BUILD_DIR)/checker-test $(BUILD_DIR)/ir-test
	"$(BUILD_DIR)/source-test"
	"$(BUILD_DIR)/lexer-test"
	"$(BUILD_DIR)/parser-test"
	"$(BUILD_DIR)/checker-test"
	"$(BUILD_DIR)/ir-test"
	sh tests/driver_test.sh "$(BUILD_DIR)/studycc"

sanitize:
	$(MAKE) BUILD_DIR=build/sanitize \
		CFLAGS="$(BASE_CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer" \
		LDFLAGS="-fsanitize=address,undefined" test

clean:
	rm -rf build
