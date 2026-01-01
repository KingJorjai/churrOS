# Makefile para generación de documentación PDF

# Configuración
PANDOC = pandoc

# Directorios
SECTIONS_DIR = sections

# Archivo de metadatos
METADATA = metadata.yaml

# Archivos fuente en orden
SOURCES = $(SECTIONS_DIR)/01-resumen.md \
          $(SECTIONS_DIR)/02-arquitectura-base.md \
          $(SECTIONS_DIR)/03-scheduler.md \
          $(SECTIONS_DIR)/04-memoria-virtual.md \
          $(SECTIONS_DIR)/05-conclusiones.md

# Archivos de salida
OUTPUT = memoria.pdf
OUTPUT_HTML = memoria.html

# Opciones de pandoc para PDF
PANDOC_PDF_FLAGS = --pdf-engine=lualatex \
                   --from=markdown+smart \
                   --metadata-file=$(METADATA) \
                   --filter mermaid-filter \
                   --syntax-highlighting=tango \
                   -V listings=true \
                   -V code-block-font-size=\small \
                   -V listings-options=breaklines=true,breakatwhitespace=true,language=C

# Opciones de pandoc para HTML
PANDOC_HTML_FLAGS = --from=markdown+smart \
                    --to=html5 \
                    --standalone \
                    --metadata-file=$(METADATA) \
                    --syntax-highlighting=tango \
                    --mathjax

# Regla principal
.PHONY: all
all: pdf

# Compilar PDF
.PHONY: pdf
pdf: $(OUTPUT)

# Compilar HTML
.PHONY: html
html: $(OUTPUT_HTML)

# Generar PDF
$(OUTPUT): $(METADATA) $(SOURCES)
	@echo "================================================"
	@echo "Compilando memoria a PDF con pandoc + lualatex..."
	@echo "================================================"
	$(PANDOC) $(PANDOC_PDF_FLAGS) $(SOURCES) -o $(OUTPUT)
	@echo "✓ PDF generado: $(OUTPUT)"

# Generar HTML
$(OUTPUT_HTML): $(METADATA) $(SOURCES)
	@echo "================================================"
	@echo "Compilando memoria a HTML..."
	@echo "================================================"
	$(PANDOC) $(PANDOC_HTML_FLAGS) $(SOURCES) -o $(OUTPUT_HTML)
	@echo "✓ HTML generado: $(OUTPUT_HTML)"

# Limpiar archivos generados
.PHONY: clean
clean:
	@echo "Limpiando archivos generados..."
	rm -f $(OUTPUT) $(OUTPUT_HTML)
	rm -f *.aux *.log *.out *.toc
	@echo "✓ Limpieza completada"

# Ver el PDF generado
.PHONY: view
view: $(OUTPUT)
	@if command -v xdg-open > /dev/null 2>&1; then \
		xdg-open $(OUTPUT); \
	elif command -v evince > /dev/null 2>&1; then \
		evince $(OUTPUT) & \
	elif command -v okular > /dev/null 2>&1; then \
		okular $(OUTPUT) & \
	else \
		echo "No se encontró visor de PDF. Abre manualmente: $(OUTPUT)"; \
	fi

# Alias de view
.PHONY: preview
preview: view

# Modo watch - regenera automáticamente al detectar cambios
.PHONY: watch
watch:
	@echo "Modo watch activado. Presiona Ctrl+C para detener."
	@while true; do \
		inotifywait -e modify $(SOURCES) $(METADATA) 2>/dev/null && \
		make --no-print-directory pdf; \
	done

# Verificar dependencias
.PHONY: check-deps
check-deps:
	@echo "Verificando dependencias..."
	@command -v $(PANDOC) >/dev/null 2>&1 || { \
		echo "ERROR: pandoc no está instalado."; \
		echo "Instala con: sudo apt install pandoc texlive-xetex texlive-lang-spanish"; \
		exit 1; \
	}
	@command -v xelatex >/dev/null 2>&1 || { \
		echo "ERROR: xelatex no está instalado."; \
		echo "Instala con: sudo apt install texlive-xetex texlive-lang-spanish"; \
		exit 1; \
	}
	@echo "✓ Todas las dependencias están instaladas"

# Ayuda
.PHONY: help
help:
	@echo "Makefile para la memoria del proyecto churrOS"
	@echo ""
	@echo "Objetivos disponibles:"
	@echo "  make all        - Compilar la memoria a PDF (default)"
	@echo "  make pdf        - Compilar la memoria a PDF"
	@echo "  make html       - Compilar la memoria a HTML"
	@echo "  make view       - Ver el PDF generado"
	@echo "  make preview    - Alias de view"
	@echo "  make watch      - Regenerar PDF automáticamente al detectar cambios"
	@echo "  make clean      - Limpiar archivos generados"
	@echo "  make check-deps - Verificar dependencias necesarias"
	@echo "  make help       - Mostrar esta ayuda"
	@echo ""
	@echo "Ejemplo de uso típico:"
	@echo "  make pdf        # Generar PDF"
	@echo "  make view       # Ver el resultado"
