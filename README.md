# Sistema de Videovigilancia y Reconocimiento Facial Embebido (RPI Zero 2W)

Este repositorio contiene la implementación de un sistema de seguridad inteligente diseñado para operar en hardware de recursos limitados, optimizado específicamente para la **Raspberry Pi Zero 2W**. El sistema integra visión artificial, procesamiento concurrente y transmisión de video en red, funcionando de manera autónoma ("Headless") y segura.

---

## 1. Definición del Problema

El desarrollo de sistemas de reconocimiento facial suele requerir hardware costoso (GPUs, procesadores de alto rendimiento). El desafío de este proyecto fue **implementar un sistema de reconocimiento biométrico funcional en tiempo real sobre un dispositivo de bajo coste y bajo consumo (Single Board Computer)**, gestionando recursos limitados (512MB RAM) y operando sin periféricos.

**Retos principales abordados:**
* **Escasez de Memoria:** Gestión estricta de recursos y Swap para compilación y ejecución.
* **Latencia:** Procesamiento de video y streaming simultáneo sin bloqueo.
* **Entorno Headless:** Operación autónoma sin monitor, con interfaz web universal.
* **Seguridad:** Autenticación robusta sin bases de datos vulnerables.

---

## 2. Arquitectura y Solución Técnica

La solución se diseñó modularmente en **C++17** para maximizar el rendimiento, utilizando programación multihilo (`std::thread`) para desacoplar la visión artificial de la transmisión de red.

### Diagrama de Arquitectura del Sistema

```mermaid
graph TD
    subgraph "Periféricos / Hardware"
        CAM[Cámara (USB/IP)] -->|Frames Raw| MAIN_THREAD
        MAIN_THREAD -->|Pixeles Directos| HDMI[Salida HDMI / Framebuffer]
    end

    subgraph "Raspberry Pi Zero 2W (Software)"
        direction TB
        
        subgraph "Hilo Principal (Procesamiento)"
            MAIN_THREAD[Captura & Visión]
            OPENCV[OpenCV: Haar + LBPH]
            MAIN_THREAD <--> OPENCV
        end

        MEMORIA[(Memoria Compartida<br>Mutex Protegido)]

        subgraph "Hilo Secundario (Red)"
            WEB_SERVER[Servidor Web TCP]
            PAM[Linux PAM Auth]
            WEB_SERVER -.->|Valida Credenciales| PAM
        end
    end

    subgraph "Cliente Remoto"
        BROWSER[Navegador Web]
    end

    MAIN_THREAD -->|Escribe JPEG| MEMORIA
    MEMORIA -->|Lee JPEG| WEB_SERVER
    WEB_SERVER -->|Stream MJPEG| BROWSER
````

### Bloque A: Adquisición y Preprocesamiento

  * **Entrada Flexible:** El sistema acepta tanto cámaras físicas (índice `0`) como flujos de red (`http://...`, `rtsp://...`).
  * **Optimización:** Redimensionamiento a **640x480** y limitación a **15 FPS** para estabilidad térmica y de CPU.

### Bloque B: Núcleo de Visión Artificial

  * **Detección (Haar Cascades):** Algoritmo rápido y ligero para localizar rostros.
  * **Reconocimiento (LBPH):** *Local Binary Patterns Histograms*. Entrena un modelo al inicio con las fotos en `assets/faces/` para identificar personas ("Johan" vs "Desconocido") con un nivel de confianza calculado.

### Bloque C: Interfaz Web y Streaming

  * **Protocolo MJPEG:** Transmisión de video mediante `multipart/x-mixed-replace`, compatible con cualquier navegador moderno sin plugins.
  * **Sincronización:** Uso de `std::mutex` para proteger el acceso a la memoria de video compartida entre el hilo de captura y el servidor web.

### Bloque D: Seguridad (Linux PAM)

  * **Autenticación Real:** Validación de credenciales contra los usuarios del sistema operativo (`/etc/shadow`) usando `security/pam_appl.h`. El acceso a la web requiere usuario y contraseña reales de la Raspberry Pi.

### Diagrama de Flujo de Datos

```mermaid
sequenceDiagram
    participant Cam as Cámara
    participant Main as Hilo Principal
    participant Shared as Memoria (Mutex)
    participant Web as Hilo Web
    participant User as Usuario Remoto

    Note over Main: Inicio del Sistema
    Main->>Main: Cargar Haar & Entrenar LBPH
    
    loop Bucle de Video (15 FPS)
        Cam->>Main: Frame Nuevo
        Main->>Main: Detectar Caras
        Main->>Main: Reconocer & Dibujar
        Main->>Main: Comprimir a JPEG
        Main->>Shared: Actualizar Frame Global (Lock)
    end

    User->>Web: GET /video_feed
    Web->>Web: Verificar Usuario (PAM)
    
    loop Streaming Web
        Web->>Shared: Leer Frame Global (Lock)
        Shared-->>Web: Datos JPEG
        Web-->>User: Enviar Parte MJPEG (--frame)
    end
```

-----

## 3\. Estructura del Proyecto

```text
DemoProyecto/
├── assets/
│   ├── haarcascades/    # Modelos de detección (descarga automática)
│   └── faces/           # BASE DE DATOS DE ROSTROS
│       ├── Johan/       # Carpeta con 10 fotos de Johan
│       └── Visitante/   # Otras carpetas...
├── src/
│   ├── sistema_final.cpp # Código fuente principal
│   └── CMakeLists.txt    # Configuración de compilación
├── scripts/
│   ├── install_dependencies.sh # Instala librerías y modelos
│   ├── build.sh                # Compila el ejecutable (limpia caché)
│   └── run_demo.sh             # Script de arranque (detecta permisos)
├── logs/                # Historial de accesos en CSV
└── README.md            # Documentación
```

-----

## 4\. Instalación y Puesta en Marcha

### Paso 1: Configuración de Hardware (Crítico para Pi Zero 2W)

Antes de nada, aumenta el archivo de intercambio (Swap) para evitar que la compilación falle por falta de RAM:

```bash
sudo nano /etc/dphys-swapfile
# Cambiar la línea a: CONF_SWAPSIZE=2048
sudo /etc/init.d/dphys-swapfile restart
```

### Paso 2: Instalación de Dependencias

Ejecuta el script de preparación. Instalará compiladores, OpenCV (+contrib), GStreamer y corregirá dependencias obsoletas.

```bash
cd DemoProyecto
chmod +x scripts/*.sh
./scripts/install_dependencies.sh
```

### Paso 3: Entrenamiento (Carga de Rostros)

1.  Crea una carpeta en `assets/faces/` con el nombre de la persona (ej. `assets/faces/Johan`).
2.  Sube al menos **10 fotos** claras del rostro de esa persona.
3.  El sistema entrenará el modelo automáticamente al arrancar.

### Paso 4: Compilación

Genera el ejecutable binario:

```bash
./scripts/build.sh
```

-----

## 5\. Ejecución y Uso

### Modo Manual

Para pruebas o depuración en tiempo real:

  * **Con Cámara USB:**
    ```bash
    ./scripts/run_demo.sh
    ```
  * **Con Stream Remoto (Pruebas de Red):**
    ```bash
    ./scripts/run_demo.sh "[http://192.168.1.50:5000/video_feed](http://192.168.1.50:5000/video_feed)"
    ```

### Acceso Web

Desde cualquier PC o móvil en la misma red:

  * **URL:** `http://<IP_RASPBERRY>:8080`
  * **Login:** Tu usuario y contraseña de la Raspberry (ej. `johan` / `*****`).

-----

## 6\. Automatización (Servicio Systemd)

Para que el sistema funcione como una cámara de seguridad autónoma al enchufarla:

1.  **Crear servicio:**
    ```bash
    sudo nano /etc/systemd/system/face_access.service
    ```
2.  **Contenido:**
    ```ini
    [Unit]
    Description=Sistema de Control de Acceso Facial
    After=network.target video.target

    [Service]
    Type=simple
    User=root
    WorkingDirectory=/home/johan/DemoProyecto
    ExecStart=/bin/bash /home/johan/DemoProyecto/scripts/run_demo.sh
    Restart=always
    RestartSec=5
    StandardOutput=journal
    StandardError=journal

    [Install]
    WantedBy=multi-user.target
    ```
3.  **Activar:**
    ```bash
    sudo systemctl daemon-reload
    sudo systemctl enable face_access.service
    sudo systemctl start face_access.service
    ```

> **⚠️ NOTA IMPORTANTE SOBRE EL CICLO DE VIDA:**
> El servicio `face_access.service` únicamente se encarga de **EJECUTAR** el programa ya compilado (`run_demo.sh`).
> El servicio **NO compila** el código. Si realizas cambios en `src/sistema_final.cpp`, debes:
>
> 1.  Detener el servicio: `sudo systemctl stop face_access.service`
> 2.  Compilar manualmente: `./scripts/build.sh`
> 3.  Iniciar el servicio: `sudo systemctl start face_access.service`

-----

## 7\. Plan de Pruebas y Resultados

  * **Latencia:** \~300-500ms en red local (aceptable para vigilancia).
  * **Estabilidad:** Operación continua validada por 4h+ sin fugas de memoria.
  * **Recuperación:** Reinicio automático en \<5s tras fallo de cámara o red.
  * **Headless:** Detección automática de ausencia de monitor para desactivar salida HDMI y evitar errores.

-----

## 8\. Análisis de Limitaciones de los Modelos

Dadas las restricciones de hardware (CPU ARMv8 limitada y sin NPU/GPU dedicada), se seleccionaron algoritmos clásicos sobre Deep Learning. Es crucial entender sus limitaciones operativas:

### Haar Cascades (Detección)

  * **Sensibilidad a la Rotación:** El clasificador `frontalface_default` es estrictamente para rostros frontales. Si el sujeto gira la cabeza más de \~30 grados o la inclina lateralmente, la detección fallará.
  * **Falsos Positivos:** En entornos con texturas complejas o sombras duras, puede detectar objetos inanimados como rostros. Se mitigó ajustando el parámetro `minNeighbors` a 4.
  * **Iluminación:** Es sensible a cambios drásticos de luz (contraluz fuerte), lo que puede impedir la detección inicial.

### LBPH (Reconocimiento)

  * **Dependencia de Datos:** A diferencia de las redes neuronales (CNN) que extraen características profundas ("embeddings"), LBPH compara histogramas de píxeles locales. Esto significa que requiere que las fotos de entrenamiento tengan una iluminación y pose similares a las condiciones reales de operación.
  * **Confianza Heurística:** El valor de "confianza" devuelto es una distancia euclidiana, no una probabilidad porcentual. Valores bajos (\<50) son mejores, pero el umbral de corte debe calibrarse empíricamente según la luz del entorno.
  * **Escalabilidad:** El tiempo de entrenamiento y predicción crece linealmente con el número de usuarios, lo que lo hace ideal para pocas personas (1-5) pero ineficiente para grandes bases de datos en este hardware.

-----

## 9\. Tecnologías Utilizadas

  * **Lenguaje:** C++17 (GCC 11.4)
  * **Visión:** OpenCV 4.6.0 (Core, ObjDetect, Face, HighGUI)
  * **Seguridad:** Linux PAM
  * **Build:** CMake 3.22
  * **OS:** Raspberry Pi OS Lite (64-bit)

-----

*Proyecto desarrollado para la asignatura de Programación de Sistemas Linux Embebidos - Universidad Nacional de Colombia.*