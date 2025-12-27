---
title: "churrOS - Simulador de Kernel con Scheduling Adaptativo"
subtitle: "Práctica de Sistemas Operativos"
author: "Jorge Arévalo Fernández"
date: "Enero 2025"
lang: es-ES
geometry: margin=2.5cm
fontsize: 11pt
toc: true
toc-depth: 3
numbersections: true
header-includes: |
  \usepackage{fancyhdr}
  \pagestyle{fancy}
  \usepackage{graphicx}
  \usepackage{listings}
  \usepackage{xcolor}
  \lstset{
    basicstyle=\ttfamily\footnotesize,
    breaklines=true,
    frame=single,
    language=C,
    commentstyle=\color{gray},
    keywordstyle=\color{blue},
    stringstyle=\color{red}
  }
---

\newpage

# Resumen Ejecutivo

**churrOS** es un simulador multihilo de un kernel de sistema operativo implementado en C utilizando `pthread.h`. El proyecto simula los componentes fundamentales de un sistema operativo moderno, incluyendo gestión de procesos, algoritmos de scheduling, y gestión de memoria virtual con paginación.

## Características Principales

- **Arquitectura event-driven**: El scheduler se activa únicamente ante eventos específicos (quantum expirado, proceso terminado, nuevo proceso)
- **Tres algoritmos de scheduling implementados**:
  - Round Robin (quantum fijo)
  - FIFO (sin preemption)
  - **Chocolate Caliente** (quantum adaptativo basado en temperatura - innovación propia)
- **Arquitectura hardware configurable**: CPUs, cores y HW threads personalizables
- **Gestión de memoria virtual completa**: MMU, TLB, paginación de 4KB
- **Suite de 19 tests automatizados** con validación de comportamiento
- **Sistema de logging multi-nivel** con soporte de colores y ubicación

## Motivación

El objetivo del proyecto es comprender en profundidad el funcionamiento interno de un kernel de sistema operativo mediante la implementación práctica de sus componentes esenciales. A diferencia de un enfoque puramente teórico, churrOS permite observar y experimentar con:

1. **Sincronización multihilo real** usando primitivas POSIX
2. **Algoritmos de scheduling** en acción con métricas medibles
3. **Gestión de memoria virtual** con traducción de direcciones
4. **Arquitectura modular** que refleja la separación de responsabilidades en un SO real

## Organización del Documento

Este documento está organizado siguiendo las tres partes principales del proyecto:

- **Parte 1**: Arquitectura base (Clock, Timer, sincronización)
- **Parte 2**: Scheduler y algoritmos de scheduling
- **Parte 3**: Gestión de memoria virtual (MMU, TLB, loader)

Cada sección incluye diseño, implementación, decisiones técnicas y resultados de testing.

\newpage
