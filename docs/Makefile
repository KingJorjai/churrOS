# Makefile para generación de documentación PDF

PANDOC = pandoc
PANDOC_FLAGS = --pdf-engine=xelatex \
               -f markdown+smart \
               --highlight-style=tango \
               --number-sections \
               --toc \
               --toc-depth=3

# Archivos fuente en orden
SOURCES = memoria/memoria.md \
          memoria/parte1.md \
          memoria/parte2.md \
          memoria/parte3.md \
          memoria/conclusiones.md

# Archivo de salida
OUTPUT = memoria.pdf

# Regla principal
all: $(OUTPUT)

# Generar PDF
$(OUTPUT): $(SOURCES)
	@echo "Generando PDF de documentación..."
	$(PANDOC) $(PANDOC_FLAGS) $(SOURCES) -o $(OUTPUT)
	@echo "✓ Documentación generada: $(OUTPUT)"

# Limpiar archivos generados
clean:
	rm -f $(OUTPUT)
	@echo "✓ Archivos generados eliminados"

# Preview del PDF (Linux)
preview: $(OUTPUT)
	@if command -v xdg-open > /dev/null; then \
		xdg-open $(OUTPUT); \
	elif command -v evince > /dev/null; then \
		evince $(OUTPUT) & \
	elif command -v okular > /dev/null; then \
		okular $(OUTPUT) & \
	else \
		echo "No se encontró visor de PDF. Abre manualmente: $(OUTPUT)"; \
	fi

# Modo watch - regenera automáticamente al detectar cambios
watch:
	@echo "Modo watch activado. Presiona Ctrl+C para detener."
	@while true; do \
		inotifywait -e modify $(SOURCES) 2>/dev/null && \
		make --no-print-directory; \
	done

# Verificar dependencias
check-deps:
	@echo "Verificando dependencias..."
	@command -v pandoc >/dev/null 2>&1 || \
		{ echo "✗ pandoc no encontrado. Instala con: sudo apt install pandoc"; exit 1; }
	@command -v xelatex >/dev/null 2>&1 || \
		{ echo "✗ xelatex no encontrado. Instala con: sudo apt install texlive-xetex texlive-lang-spanish"; exit 1; }
	@echo "✓ Todas las dependencias están instaladas"

# Ayuda
help:
	@echo "Targets disponibles:"
	@echo "  make          - Generar PDF de documentación"
	@echo "  make clean    - Eliminar archivos generados"
	@echo "  make preview  - Abrir PDF en visor"
	@echo "  make watch    - Regenerar automáticamente al detectar cambios"
	@echo "  make check-deps - Verificar que las dependencias están instaladas"
	@echo "  make help     - Mostrar esta ayuda"

.PHONY: all clean preview watch check-deps help
