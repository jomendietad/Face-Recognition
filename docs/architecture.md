# Arquitectura

Este proyecto es una demo simple para detección de rostros usando OpenCV en C++.

Componentes:

- `src/`: Contiene el ejecutable `demo_face` que realiza la captura (webcam o imagen) y usa un clasificador Haar para detectar rostros.
- `assets/haarcascades/`: Almacena los modelos de cascada (XML) necesarios para la detección.
- `scripts/`: Scripts para instalar dependencias, compilar y ejecutar la demo.
- `docs/`: Documentación con guía de uso y arquitectura.

Flujo:
1. Instalar dependencias (OpenCV).  
2. Construir con CMake.  
3. Ejecutar la demo, que carga el cascade y procesa frames desde la cámara o una imagen.
