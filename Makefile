# Nomi e Tool
LIB_NAME = libtasicmd.a
CC = gcc
AR = ar
CFLAGS = -Wall -Wextra -std=c99 -Iinc -g
ARFLAGS = rcs

# Directory
SRCDIR = src
INCDIR = inc
TESTDIR = tests
BINDIR = bin

# Percorsi File
SRC = $(SRCDIR)/tasicmd.c
OBJ = $(BINDIR)/tasicmd.o
LIB = $(BINDIR)/$(LIB_NAME)
TEST_SRC = $(TESTDIR)/test_suite.c
TEST_BIN = $(BINDIR)/test_runner

.PHONY: all clean test lib

# Target principale: genera la libreria statica
all: lib

# Crea la cartella bin se non esiste
$(BINDIR):
	mkdir -p $(BINDIR)

# 1. Compila il sorgente in un file oggetto
$(OBJ): $(SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# 2. Crea la libreria statica (.a) partendo dall'oggetto
lib: $(LIB)

$(LIB): $(OBJ)
	$(AR) $(ARFLAGS) $@ $^
	@echo "Libreria statica creata: $@"

# 3. Compila ed esegue i test linkando la libreria statica
# Nota: L'ordine dei file nel comando gcc è importante per il linker
test: lib
	$(CC) $(CFLAGS) $(TEST_SRC) -L$(BINDIR) -ltasicmd -o $(TEST_BIN)
# 	@echo "--- Esecuzione Unit Test ---"
# 	./$(TEST_BIN)

clean:
	rm -rf $(BINDIR)