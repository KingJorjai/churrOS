
CC := gcc
CFLAGS := -Wall -Wextra -O2 -Iinclude
LDFLAGS := -lpthread

# Fuentes y objetos de la biblioteca
SRC := source/clock.c
OBJ := build/clock.o
LIB := build/libclock.a

# Tests
TEST_SRC := tests/test_clock.c
TEST_BIN := build/test_clock

.PHONY: all clean test run-test

all: $(LIB)

build:
	mkdir -p build

$(OBJ): build $(SRC)
	$(CC) $(CFLAGS) -c $(SRC) -o $(OBJ)

$(LIB): $(OBJ)
	@mkdir -p $(dir $(LIB))
	ar rcs $(LIB) $(OBJ)

# Compilar tests
$(TEST_BIN): $(LIB) $(TEST_SRC)
	$(CC) $(CFLAGS) $(TEST_SRC) -L./build -lclock $(LDFLAGS) -o $(TEST_BIN)

# Target para compilar tests
test: $(TEST_BIN)
	@echo "Tests compilados exitosamente: $(TEST_BIN)"

# Target para ejecutar tests
run-test: $(TEST_BIN)
	@echo "Ejecutando tests..."
	@./$(TEST_BIN)

clean:
	rm -rf build
