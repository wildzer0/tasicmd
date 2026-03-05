LIB_NAME = libtcmd.a
CC = gcc
AR = ar
CFLAGS = -Wall -Wextra -std=c99 -Iinc -g
ARFLAGS = rcs

SRCDIR = src
INCDIR = inc
BINDIR = bin

SRC = $(SRCDIR)/tasicmd.c
OBJ = $(BINDIR)/tasicmd.o
LIB = $(BINDIR)/$(LIB_NAME)

.PHONY: all clean lib

all: lib

$(BINDIR):
	mkdir -p $(BINDIR)

$(OBJ): $(SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -c $< -o $@

lib: $(LIB)

$(LIB): $(OBJ)
	$(AR) $(ARFLAGS) $@ $^

clean:
	rm -rf $(BINDIR)