# Documentación del Proyecto churrOS

Este directorio contiene la documentación completa del proyecto churrOS en formato Markdown, lista para ser convertida a PDF mediante Pandoc.

## Estructura

```
docs/
├── Makefile              # Automatización de generación de PDF
├── README.md             # Este archivo
├── memoria.pdf           # PDF generado (no versionado en Git)
├── memoria/
│   ├── memoria.md        # Portada y resumen ejecutivo
│   ├── parte1.md         # Parte 1: Arquitectura base
│   ├── parte2.md         # Parte 2: Scheduler y algoritmos
│   ├── parte3.md         # Parte 3: Memoria virtual
│   ├── conclusiones.md   # Conclusiones y trabajo futuro
│   └── imagenes/         # Directorio para imágenes (opcional)
└── Enunciado/            # PDFs del enunciado original
```

## Requisitos

### Ubuntu/Debian

```bash
sudo apt update
sudo apt install pandoc texlive-xetex texlive-latex-extra texlive-lang-spanish
```

### Fedora/RHEL

```bash
sudo dnf install pandoc texlive-xetex texlive-collection-langspanish
```

### macOS (Homebrew)

```bash
brew install pandoc
brew install --cask mactex
```

### Verificar Instalación

```bash
cd docs/
make check-deps
```

Salida esperada:
```
✓ Todas las dependencias están instaladas
```

## Generación del PDF

### Método Simple

```bash
cd docs/
make
```

Esto genera el archivo `memoria.pdf` en el directorio `docs/`.

### Visualizar el PDF

```bash
make preview
```

Abre el PDF en el visor predeterminado del sistema.

### Regeneración Automática

Para desarrollo activo de la documentación:

```bash
make watch
```

El PDF se regenerará automáticamente cada vez que modifiques un archivo `.md`.

## Personalización

### Modificar Metadatos

Edita el encabezado YAML en `memoria/memoria.md`:

```yaml
---
title: "Tu Título"
author: "Tu Nombre"
date: "Fecha"
# ... otros campos ...
---
```

### Añadir Imágenes

1. Guarda las imágenes en `memoria/imagenes/`
2. Referéncialas en Markdown:

```markdown
![Descripción de la imagen](imagenes/mi-diagrama.png)
```

### Cambiar Estilo de Código

Modifica el flag `--highlight-style` en el `Makefile`:

```makefile
PANDOC_FLAGS = ... --highlight-style=tango
```

Estilos disponibles: `tango`, `pygments`, `kate`, `monochrome`, `breezedark`, `espresso`, `zenburn`, `haddock`.

## Comandos Disponibles

| Comando | Descripción |
|---------|-------------|
| `make` | Generar PDF |
| `make clean` | Eliminar PDF generado |
| `make preview` | Abrir PDF en visor |
| `make watch` | Regenerar automáticamente |
| `make check-deps` | Verificar dependencias |
| `make help` | Mostrar ayuda |

## Contenido de la Documentación

### Memoria (memoria.md)

- Portada con metadatos
- Resumen ejecutivo
- Motivación del proyecto
- Organización del documento

### Parte 1 (parte1.md)

- Arquitectura general del sistema
- Clock: motor del simulador
- Timer: interrupciones periódicas
- Process Control Block (PCB)
- Process Queue (cola thread-safe)
- Machine: simulación hardware
- Sincronización con mutex y condvars

### Parte 2 (parte2.md)

- Arquitectura del scheduler event-driven
- Round Robin (RR): quantum fijo
- FIFO: sin preemption
- **Chocolate Caliente (CH)**: quantum adaptativo por temperatura
- Comparativa de algoritmos
- Sistema de logging multi-nivel
- Suite de 19 tests automatizados

### Parte 3 (parte3.md)

- Arquitectura de memoria virtual
- Paginación de 4KB
- Memory Management Unit (MMU) con TLB
- Proceso de traducción de direcciones
- Instruction set (LD, ST, ADD, EXIT)
- Loader de programas .elf
- Prometheus: generador de programas

### Conclusiones (conclusiones.md)

- Resumen de logros
- Análisis de resultados
- Comparación con sistemas reales
- Desafíos encontrados
- Trabajo futuro
- Aplicaciones educativas
- Referencias y bibliografía

## Formato del PDF

El PDF generado incluye:

- ✅ Portada con título, autor y fecha
- ✅ Tabla de contenidos automática
- ✅ Numeración de secciones
- ✅ Syntax highlighting para código C
- ✅ Márgenes profesionales (2.5cm)
- ✅ Fuente de 11pt
- ✅ Encabezados y paginación
- ✅ Soporte para español (acentos, ñ, etc.)

## Solución de Problemas

### Error: "pandoc: command not found"

```bash
# Ubuntu/Debian
sudo apt install pandoc

# Fedora
sudo dnf install pandoc

# macOS
brew install pandoc
```

### Error: "xelatex not found"

```bash
# Ubuntu/Debian
sudo apt install texlive-xetex texlive-latex-extra

# Fedora
sudo dnf install texlive-xetex

# macOS
brew install --cask mactex
```

### Error: "Package babel Error"

Instala el paquete de idioma español:

```bash
# Ubuntu/Debian
sudo apt install texlive-lang-spanish

# Fedora
sudo dnf install texlive-collection-langspanish
```

### El PDF se genera pero falta formato

Instala paquetes LaTeX adicionales:

```bash
# Ubuntu/Debian
sudo apt install texlive-fonts-recommended texlive-fonts-extra

# Fedora
sudo dnf install texlive-collection-fontsrecommended
```

### Advertencias de "Overfull hbox"

Esto es normal en LaTeX. Si quieres eliminarlas, ajusta el texto largo o tablas anchas.

## Alternativas a Pandoc

Si Pandoc no está disponible, puedes:

### 1. Usar Typora (WYSIWYG)

1. Descarga Typora: https://typora.io/
2. Abre `memoria/memoria.md`
3. File → Export → PDF

### 2. Usar VS Code con extensiones

1. Instala extensión "Markdown PDF"
2. Abre cualquier `.md`
3. Ctrl+Shift+P → "Markdown PDF: Export (pdf)"

### 3. Usar servicios online

- **Dillinger**: https://dillinger.io/ (pega contenido, exporta PDF)
- **Markdown to PDF**: https://www.markdowntopdf.com/

## Tips para Edición

### Formato de Código

```markdown
\```c
int main() {
    printf("Hello, world!\n");
    return 0;
}
\```
```

### Tablas

```markdown
| Columna 1 | Columna 2 | Columna 3 |
|-----------|-----------|-----------|
| Dato 1    | Dato 2    | Dato 3    |
```

### Listas

```markdown
- Item 1
- Item 2
  - Subitem 2.1
  - Subitem 2.2
```

### Enlaces

```markdown
[Texto del enlace](https://github.com/KingJorjai/churrOS)
```

### Notas al pie

```markdown
Esto es un texto con referencia[^1].

[^1]: Esta es la nota al pie.
```

## Entrega del Proyecto

Para entregar la documentación:

```bash
# 1. Generar el PDF
cd docs/
make

# 2. Verificar que se generó correctamente
ls -lh memoria.pdf

# 3. Copiar a directorio de entrega (si es necesario)
cp memoria.pdf ~/entrega/

# 4. O crear archivo comprimido con todo
cd ..
tar -czf churrOS-documentacion.tar.gz docs/memoria.pdf README.md
```

El archivo `memoria.pdf` es autónomo y contiene toda la documentación del proyecto.

## Contribuciones

Si encuentras errores o quieres mejorar la documentación:

1. Edita los archivos `.md` correspondientes
2. Regenera el PDF con `make`
3. Verifica el resultado con `make preview`
4. Commit y push:

```bash
git add docs/memoria/*.md
git commit -m "docs: actualizar sección X"
git push
```

## Licencia

La documentación está bajo la misma licencia que el proyecto principal (ver archivo LICENSE en el directorio raíz).

---

**Autor**: Jorge Arévalo Fernández
**Fecha**: Diciembre 2025  
**Proyecto**: churrOS - Simulador de Kernel con Scheduling Adaptativo
