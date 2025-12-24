
CC := gcc
CFLAGS := -Wall -Wextra -O2 -Iinclude
LDFLAGS := -lpthread

# Directorios
SRC_DIR := source
BUILD_DIR := build
INCLUDE_DIR := include
TEST_DIR := tests
PROMETHEUS_DIR := prometheus
ELF_DIR := elfs

# Fuentes del kernel
KERNEL_SOURCES := $(SRC_DIR)/clock.c \
                  $(SRC_DIR)/timer.c \
                  $(SRC_DIR)/pcb.c \
                  $(SRC_DIR)/process_queue.c \
                  $(SRC_DIR)/machine.c \
				  $(SRC_DIR)/logging.c \
				  $(SRC_DIR)/scheduler.c \
				  $(SRC_DIR)/memory.c \
				  $(SRC_DIR)/instruction.c \
				  $(SRC_DIR)/loader.c \
                  $(SRC_DIR)/kernel.c \
                  $(SRC_DIR)/main.c

# Objetos del kernel
KERNEL_OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(KERNEL_SOURCES))

# Ejecutable principal
KERNEL_BIN := $(BUILD_DIR)/churros

# Prometheus
PROMETHEUS_BIN := $(BUILD_DIR)/prometheus
PROMETHEUS_SRC := $(PROMETHEUS_DIR)/prometheus.c
PROMETHEUS_FLAGS := -I$(PROMETHEUS_DIR)
# Semilla fija para reproducibilidad entre instancias del repositorio
PROMETHEUS_SEED := 42

# Biblioteca (para tests)
LIB_SOURCES := $(SRC_DIR)/clock.c \
               $(SRC_DIR)/timer.c \
               $(SRC_DIR)/pcb.c \
               $(SRC_DIR)/process_queue.c \
               $(SRC_DIR)/machine.c \
			   $(SRC_DIR)/kernel.c \
			   $(SRC_DIR)/logging.c \
			   $(SRC_DIR)/memory.c \
			   $(SRC_DIR)/instruction.c \
			   $(SRC_DIR)/loader.c
LIB_OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/lib_%.o,$(LIB_SOURCES))
LIB := $(BUILD_DIR)/libchurros.a

# Tests
TEST_SRC := $(TEST_DIR)/test_clock.c
TEST_BIN := $(BUILD_DIR)/test_clock

.PHONY: all clean test run-test run prometheus elfs clean-elfs

all: $(KERNEL_BIN)

# Crear directorio build
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compilar objetos del kernel
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compilar objetos de la biblioteca (sin main)
$(BUILD_DIR)/lib_%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Enlazar el ejecutable del kernel
$(KERNEL_BIN): $(KERNEL_OBJECTS)
	$(CC) $(CFLAGS) $(KERNEL_OBJECTS) $(LDFLAGS) -o $(KERNEL_BIN)
	@echo "Kernel compilado exitosamente: $(KERNEL_BIN)"

# Compilar prometheus
$(PROMETHEUS_BIN): $(PROMETHEUS_SRC) | $(BUILD_DIR)
	$(CC) -Wall -ggdb $(PROMETHEUS_FLAGS) $(PROMETHEUS_SRC) -o $(PROMETHEUS_BIN)
	@echo "Prometheus compilado exitosamente: $(PROMETHEUS_BIN)"

# Target para compilar prometheus
prometheus: $(PROMETHEUS_BIN)

# Target para generar archivos .elf
# Usa semilla fija ($(PROMETHEUS_SEED)) para garantizar reproducibilidad
elfs: $(PROMETHEUS_BIN)
	@mkdir -p $(ELF_DIR)
	@echo "Generando archivos .elf con semilla $(PROMETHEUS_SEED)..."
	@cd $(ELF_DIR) && ../$(PROMETHEUS_BIN) -s $(PROMETHEUS_SEED) -nprog -f0 -l20 -p60
	@echo "Archivos .elf generados en $(ELF_DIR)/"

# Crear biblioteca estática (para tests)
$(LIB): $(LIB_OBJECTS)
	ar rcs $(LIB) $(LIB_OBJECTS)

# Compilar tests
$(TEST_BIN): $(LIB) $(TEST_SRC)
	$(CC) $(CFLAGS) $(TEST_SRC) -L./build -lchurros $(LDFLAGS) -o $(TEST_BIN)

# Target para compilar tests
test: $(TEST_BIN)
	@echo "Tests compilados exitosamente: $(TEST_BIN)"

# Target para ejecutar tests
run-test: $(TEST_BIN)
	@echo "Ejecutando tests..."
	@./$(TEST_BIN)

# Target para ejecutar el kernel
run: $(KERNEL_BIN)
	@echo "Ejecutando churrOS..."
	@./$(KERNEL_BIN)

clean:
	rm -rf $(BUILD_DIR)

clean-elfs:
	rm -rf $(ELF_DIR)
