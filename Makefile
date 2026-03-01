LIB_NAME = libtasicmd.a
CC = gcc
AR = ar
CFLAGS = -Wall -Wextra -std=c99 -Iinc -g
ARFLAGS = rcs

SRCDIR = src
INCDIR = inc
TESTDIR = tests
BINDIR = bin

SRC = $(SRCDIR)/tasicmd.c
OBJ = $(BINDIR)/tasicmd.o
LIB = $(BINDIR)/$(LIB_NAME)
TEST_SRC = $(TESTDIR)/test_suite.c
TEST_BIN = $(BINDIR)/test_runner

.PHONY: all clean example test lib

all: lib

$(BINDIR):
	mkdir -p $(BINDIR)

$(OBJ): $(SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -c $< -o $@

lib: $(LIB)

$(LIB): $(OBJ)
	$(AR) $(ARFLAGS) $@ $^
	@echo "Libreria statica creata: $@"

example: lib
	$(CC) $(CFLAGS) $(TEST_SRC) -L$(BINDIR) -ltasicmd -o $(TEST_BIN)

test: example
	./$(TEST_BIN)

clean:
	rm -rf $(BINDIR)