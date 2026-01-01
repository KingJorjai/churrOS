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
  \fancyhead[L]{churrOS}
  \fancyhead[R]{\thepage}
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

El proyecto implementa un scheduler event-driven que se activa únicamente ante eventos específicos como quantum expirado, proceso terminado o creación de nuevo proceso. Se han desarrollado tres algoritmos de scheduling completos: Round Robin con quantum fijo, FIFO sin preemption, y como innovación principal, Chocolate Caliente que implementa quantum adaptativo basado en temperatura del proceso.

La arquitectura hardware es completamente configurable permitiendo especificar CPUs, cores y HW threads según las necesidades de experimentación. El sistema de memoria virtual incluye MMU con TLB, paginación de 4KB y traducción completa de direcciones virtuales a físicas. La validación del sistema se realiza mediante una suite de 30 tests automatizados que verifican todos los componentes. El sistema de logging multinivel con soporte de colores facilita el debugging y comprensión del comportamiento.

## Motivación

El objetivo del proyecto es comprender en profundidad el funcionamiento interno de un kernel de sistema operativo mediante implementación práctica. A diferencia de un enfoque puramente teórico, churrOS permite experimentar directamente con sincronización multihilo usando primitivas POSIX, observar algoritmos de scheduling en acción con métricas medibles, implementar gestión de memoria virtual con traducción de direcciones, y comprender cómo la arquitectura modular refleja la separación de responsabilidades en sistemas operativos reales

## Organización del Documento

Este documento está organizado siguiendo las tres partes principales del proyecto:

- **Parte 1**: Arquitectura base (Clock, Timer, sincronización)
- **Parte 2**: Scheduler y algoritmos de scheduling
- **Parte 3**: Gestión de memoria virtual (MMU, TLB, loader)

Cada sección incluye diseño, implementación, decisiones técnicas y resultados de testing.

\newpage
