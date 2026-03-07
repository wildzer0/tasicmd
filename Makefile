LIB_NAME = libtcmd.a
TOOLCHAIN =
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
	@mkdir -p $(BINDIR)

$(OBJ): $(SRC) | $(BINDIR)
	@$(TOOLCHAIN)$(CC) $(CFLAGS) -c $< -o $@

lib: $(LIB)

$(LIB): $(OBJ)
	@$(TOOLCHAIN)$(AR) $(ARFLAGS) $@ $^

clean:
	rm -rf $(BINDIR)