# PracticasControl

> **Laboratorio Virtual de Control Automático**  
> Universidad Nacional Autónoma de México — Facultad de Ingeniería  
> Servicio Social — Clave 2025-12/151-204  
> **Autor:** Marino López Cruz
> **Coautores:** Fuentes Yañez Ivan
, Garduño Cruz Ingrid Paola
, Huarancca Panana Irvin Daymont,
 Ortíz Martínez Raúl
 
> **Supervisora:** M.I. Marcela Montserrat Trejo Guerra

Repositorio que contiene el código fuente, firmware y documentación del laboratorio virtual de control automático. El proyecto está compuesto por dos simuladores desarrollados en MATLAB App Designer y una plataforma de monitoreo y control en tiempo real para un motor DC físico basada en ESP32 + Vue 3.

---

## Contenido del repositorio

```
PracticasControl/
├── App1/                        # Simulador del péndulo simple (MATLAB App Designer)
├── App2/                        # Simulador del motor DC GM25-370 (MATLAB App Designer)
├── esp32-dashboard/             # Dashboard web de control en tiempo real (Vue 3 + Vite)
│   ├── src/
│   │   ├── App.vue              # Componente principal con toda la lógica de la UI
│   │   ├── main.js              # Punto de entrada de la aplicación
│   │   └── style.css            # Estilos globales
│   ├── Driver ESP32.ino         # Firmware del ESP32-C3 (Arduino)
│   ├── index.html
│   ├── vite.config.js
│   └── package.json
├── README.md
└── LICENSE
```

---

## Módulo 1 — Simulador del Péndulo Simple (`App1`)

### Descripción del sistema

El sistema estudiado es el **péndulo simple con fricción viscosa**, un modelo no lineal ampliamente utilizado como introducción a la teoría de control automático.

**Ecuación de movimiento:**

$$\frac{d^2\theta}{dt^2} + \frac{b}{mL}\frac{d\theta}{dt} + \frac{g}{L}\sin(\theta) = 0$$

Donde:
- `m` — masa del péndulo (kg)
- `L` — longitud de la varilla (m)
- `b` — coeficiente de fricción viscosa
- `g` — aceleración gravitacional (9.78 m/s²)

**Representación LTI (linealización alrededor de θ = 0):**

$$\begin{pmatrix}\dot{x}_1\\\dot{x}_2\end{pmatrix} = \begin{pmatrix}0 & 1\\-\frac{g}{L} & -\frac{b}{mL^2}\end{pmatrix}\begin{pmatrix}x_1\\x_2\end{pmatrix} + \begin{pmatrix}0\\\frac{1}{mL^2}\end{pmatrix}u$$

### Prácticas incluidas

#### Práctica 1 — Modelo matemático y simulación
- Ingreso de parámetros: masa `m`, longitud `L`, fricción `b` y fuerza aplicada `F`
- Ajuste de posición inicial con deslizador (0° a 360°)
- Simulación numérica con `ode45` del modelo **no linealizado**
- Animación en tiempo real del movimiento del péndulo
- Gráfica de posición angular θ(t) en radianes

#### Práctica 2 — Análisis de estabilidad (LTI)
- Selección de salida del sistema: **posición** o **velocidad**
- Funciones de transferencia disponibles:

  - Posición como salida:  
    $\frac{\Theta(s)}{U(s)} = \frac{\frac{1}{mL^2}}{s^2 + \frac{b}{mL^2}s + \frac{g}{L}}$

  - Velocidad como salida:  
    $\frac{\Omega(s)}{U(s)} = \frac{\frac{1}{mL^2}s}{s^2 + \frac{b}{mL^2}s + \frac{g}{L}}$

- Configuración de retroalimentación H(s) (numerador y denominador ingresados como vectores)
- Ganancia proporcional `Kp` ajustable
- Análisis disponibles: **Lugar Geométrico de las Raíces (LGR)**, **Diagrama de Bode** y **Criterio de Nyquist**

### Requisitos

- MATLAB R2020b o superior
- MATLAB App Designer (incluido en MATLAB)
- Control System Toolbox

### Ejecución

1. Abrir MATLAB y navegar a la carpeta `App1/`
2. Ejecutar en la ventana de comandos:
   ```matlab
   Practicas_de_laboratorio
   ```
3. La aplicación requiere los archivos de imagen `escudounam_negro.jpg` y `escudofi_negro.jpg` en el mismo directorio

---

## Módulo 2 — Simulador del Motor DC (`App2`)

### Descripción del sistema

El sistema estudiado es el **motor de corriente continua (DC)**, un sistema híbrido electromecánico compuesto por un circuito de armadura y un sistema mecánico rotacional.

**Parámetros del modelo:**
- `R` — resistencia del embobinado (Ω)
- `L` — inductancia de armadura (H)
- `b` — coeficiente de fricción viscosa (N·m·s/rad)
- `k_e` — constante de fuerza contraelectromotriz (V·s/rad)
- `k_t` — constante de torque (N·m/A)
- `J` — momento de inercia del rotor (kg·m²)

**Representación en espacio de estados:**

$$\begin{pmatrix}\dot{x}_1\\\dot{x}_2\end{pmatrix} = \begin{pmatrix}-\frac{R}{L} & -\frac{k_e}{L}\\\frac{k_t}{J} & -\frac{b}{J}\end{pmatrix}\begin{pmatrix}x_1\\x_2\end{pmatrix} + \begin{pmatrix}\frac{1}{L}\\0\end{pmatrix}u$$

Donde `x₁ = i` (corriente de armadura) y `x₂ = ω` (velocidad angular).

### Prácticas incluidas

#### Práctica 1 — Modelo matemático y simulación en lazo abierto
- Ingreso de parámetros completos del motor
- Simulación de la respuesta a un voltaje de entrada tipo escalón
- Gráfica de velocidad angular ω(t) en rad/s
- Cálculo y visualización del valor en estado estacionario

#### Práctica 2 & 3 — Análisis de estabilidad en lazo cerrado
- Selección de salida: **posición** o **velocidad**
- Funciones de transferencia disponibles:

  - Velocidad como salida:  
    $\frac{\Omega(s)}{U(s)} = \frac{k_e}{(R + Ls)(Js + b) + k_ek_t}$

  - Posición como salida:  
    $\frac{\Theta(s)}{U(s)} = \frac{k_e}{s\left[(R + Ls)(Js + b) + k_ek_t\right]}$

- Configuración de retroalimentación H(s), ganancia proporcional `Kp`
- Análisis disponibles: **LGR**, **Diagrama de Bode** y **Criterio de Nyquist**

#### Práctica 4 — Diseño de controladores PID
Basado en el motor **GM25-370** caracterizado experimentalmente (despreciando inductancia y fricción):

| Parámetro | Valor |
|-----------|-------|
| `k_e`     | 0.88671 |
| `k_t`     | 0.41675 |
| `J`       | 0.01639 kg·m² |
| `R`       | 6 Ω |

**Función de transferencia simplificada:**

$$\frac{\Omega(s)}{U(s)} = \frac{\frac{k_e}{RJ}}{s + \frac{k_ek_t}{RJ}}$$

- Diseño libre de ganancias `Kp`, `Ki`, `Kd`
- Simulación de la respuesta del sistema en lazo cerrado con referencia de velocidad configurable (0–14 rad/s)
- Visualización del tiempo de establecimiento y sobreimpulso

### Requisitos

- MATLAB R2020b o superior
- MATLAB App Designer
- Control System Toolbox

### Ejecución

1. Abrir MATLAB y navegar a la carpeta `App2/`
2. Ejecutar en la ventana de comandos:
   ```matlab
   Practicas_de_motorDC
   ```
3. La aplicación requiere los archivos de imagen `escudounam_negro.jpg`, `escudofi_negro.jpg` y `Captura de pantalla 2026-03-22 135115.png` en el mismo directorio

---

## Módulo 3 — Dashboard de Control en Tiempo Real (`esp32-dashboard`)

### Descripción

Plataforma web desarrollada con **Vue 3 + Vite** para el monitoreo y control en tiempo real del motor DC GM25-370 montado en el prototipo físico. Se comunica vía HTTP con un microcontrolador **ESP32-C3** a través de una red WiFi local.

### Funcionalidades

- **Control manual de RPM** — deslizador táctil y botones rápidos (0–120 RPM)
- **Perfil de velocidad trapezoidal** — velocidad máxima y duración configurables
- **Control de posición** — movimiento a ángulo objetivo (0°–360°) con perfil S suavizado
- **Gráficas en tiempo real** — velocidad (setpoint vs. real) y posición (setpoint vs. real), con zoom y scroll
- **Métricas automáticas** — error promedio, sobreimpulso, tiempo de establecimiento, seguimiento (%)
- **PID ajustable** en tiempo real desde la interfaz
- **Paro de emergencia** y **calibración de cero absoluto**
- **Modo simulación** — permite probar toda la interfaz sin hardware físico

### Firmware del ESP32 (`Driver ESP32.ino`)

El firmware implementa en el ESP32-C3:

- Servidor HTTP en el puerto 80 con endpoints REST
- Lectura de encoder incremental por interrupciones
- Control PI de velocidad con compensación de fricción estática
- Control de posición con perfil polinomial de quinto orden (curva S)
- Perfil de velocidad trapezoidal
- Descubrimiento por mDNS (`ESPRUL.local`)

**Pines utilizados (ESP32-C3 Supermini):**

| Pin | Función |
|-----|---------|
| 0   | Encoder canal A (interrupción) |
| 1   | Encoder canal B |
| 21  | PWM motor |
| 20  | DIR1 (dirección) |
| 7   | DIR2 (dirección) |

**Credenciales WiFi predeterminadas (modificar antes de cargar):**

```cpp
const char* ssid     = "Galaxy A71";
const char* password = "12345678";
```

**Parámetros del controlador cargados por defecto:**

```cpp
float Kp_vel = 10.13;
float Ki_vel = 100;
float Kp_pos = 1.5;
```

### Requisitos del dashboard web

- Node.js 20.19.0 o superior (recomendado ≥ 22)
- npm 8 o superior

### Instalación y servidor de desarrollo

```bash
# 1. Clonar el repositorio (si aún no lo tienes)
git clone https://github.com/<tu-usuario>/PracticasControl.git
cd PracticasControl

# 2. Entrar a la carpeta del dashboard
cd esp32-dashboard

# 3. Instalar dependencias
npm install

# 4. Iniciar el servidor de desarrollo
npm run dev
```

El servidor quedará disponible en:
```
http://localhost:5173
```
y también es accesible desde cualquier dispositivo en la misma red en:
```
http://<IP-de-tu-PC>:5173
```
(esto es útil para usar el dashboard desde un celular en la misma red WiFi que el ESP32).

### Compilar para producción

```bash
npm run build
```

Los archivos estáticos se generarán en `esp32-dashboard/dist/`. Pueden alojarse en cualquier servidor web estático.

### Previsualizar la build de producción

```bash
npm run preview
```

### Dependencias principales

| Paquete | Versión | Uso |
|---------|---------|-----|
| `vue` | ^3.5.25 | Framework UI reactivo |
| `vite` | ^7.3.1 | Bundler y servidor de desarrollo |
| `@vitejs/plugin-vue` | ^6.0.2 | Soporte de SFC para Vite |

### Endpoints del ESP32

| Método | Ruta | Descripción |
|--------|------|-------------|
| GET | `/ping` | Verificación de conexión |
| GET | `/status` | Estado completo: RPM, posición, setpoint, PID |
| GET | `/rpm?value=<n>` | Establece RPM objetivo (0–120) |
| GET | `/pid?kp=<n>&ki=<n>` | Actualiza ganancias del controlador |
| GET | `/profile/config?vmax=<n>&t=<n>` | Configura perfil trapezoidal |
| GET | `/profile/start` | Inicia perfil de velocidad |
| GET | `/profile/stop` | Detiene perfil |
| GET | `/position?grados=<n>` | Mueve a ángulo objetivo (0–360°) |
| GET | `/position/stop` | Cancela movimiento de posición |
| GET | `/set_zero` | Calibra la posición actual como cero |

---

## Instrucciones generales de configuración

### Cargar el firmware al ESP32

1. Instalar [Arduino IDE](https://www.arduino.cc/en/software) (versión 2.x recomendada)
2. Agregar el soporte para ESP32 en Arduino IDE:
   - Ir a **Archivo → Preferencias** y agregar en "URLs adicionales":
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - Instalar **esp32 by Espressif Systems** desde el Gestor de placas
3. Instalar las librerías requeridas desde el Gestor de librerías:
   - `ArduinoJson` (versión ≥ 6)
   - `ESPmDNS` (incluida con el paquete ESP32)
4. Seleccionar la placa: **ESP32C3 Dev Module**
5. Modificar las credenciales WiFi en `Driver ESP32.ino`
6. Cargar el sketch al ESP32

### Conectar el dashboard al ESP32

1. Asegurarse de que el PC/celular y el ESP32 estén en la **misma red WiFi**
2. Iniciar el servidor de desarrollo del dashboard (`npm run dev`)
3. En el dashboard, hacer clic en **Ajustes** y presionar **Buscar ESP32** o ingresar la IP manualmente
4. Alternativamente, usar el **Modo Simulación** para probar sin hardware

---

## Estructura de control (resumen)

```
Referencia ──► [ PID / Controlador ] ──► [ Motor DC ] ──► Salida (ω o θ)
                                                  ▲
                                          [ Encoder ]
                                          (retroalimentación)
```

El ESP32 ejecuta el lazo de control a **50 ms (20 Hz)**. El dashboard obtiene el estado vía polling HTTP cada 500 ms.

---

## Licencia

MIT License — Copyright (c) 2026 Marino LC  
Ver archivo [LICENSE](LICENSE) para detalles completos.

---

*Propiedad de la Facultad de Ingeniería, UNAM*
