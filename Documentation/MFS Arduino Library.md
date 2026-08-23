# 🛡️ MFS Arduino Library (Multi-Function Shield)

Una librería modular, eficiente y fácil de usar para controlar el popular **Arduino Multi-Function Shield (MFS)** en placas compatibles con Arduino (Uno R3, Leonardo, Mega 2560, Nano y equivalentes).

[![Arduino Compatible](https://img.shields.io/badge/Arduino-Compatible-00979C?logo=arduino&logoColor=white)](#)
[![License: MIT/GPL](https://img.shields.io/badge/License-GPL%20%2F%20MIT-blue.svg)](#)
[![Platform](https://img.shields.io/badge/Platform-AVR%20%2F%20MegaAVR-orange)](#)

---

## 📋 Índice
- [Características del Shield](#-características-del-shield)
- [Mapeo de Pines (Hardware Pinout)](#-mapeo-de-pines-hardware-pinout)
- [Instalación](#-instalación)
- [Inicio Rápido](#-inicio-rápido)
- [Referencia de la API](#-referencia-de-la-api)
  - [Inicialización y Timer](#1-inicialización-y-temporización)
  - [Display de 7 Segmentos (4 Dígitos)](#2-display-de-7-segmentos)
  - [Control de LEDs](#3-control-de-leds)
  - [Lectura de Pulsadores y Eventos](#4-pulsadores-y-gestión-de-eventos)
  - [Buzzer / Zumbador](#5-buzzer--zumbador)
  - [Potenciómetro y Entradas Analógicas](#6-potenciómetro-y-sensores-analógicos)
  - [Sensores Opcionales (LM35, DS18B20, Ultrasonidos)](#7-sensores-auxiliares)
- [Ejemplos de Código](#-ejemplos-de-código)
- [Consideraciones y Solución de Problemas](#-consideraciones-y-solución-de-problemas)
- [Licencia](#-licencia)

---

## 🚀 Características del Shield

El Multi-Function Shield integra en una sola placa los siguientes componentes:
- **Display LED de 7 segmentos de 4 dígitos** (controlado mediante 2 registros de desplazamiento `74HC595`).
- **4 LEDs de estado** (D1 a D4) con lógica negativa (*Active LOW*).
- **3 Pulsadores táctiles** (S1 a S3) con resistencias pull-up integradas.
- **1 Potenciómetro rotativo** de 10 kΩ conectado al canal analógico `A0`.
- **1 Zumbador piezoeléctrico** (Buzzer) pasivo o activo en pin `D3`.
- **Puerto de expansión** para sensor de temperatura LM35 / DS18B20 (`A4`).
- **Puerto de receptor IR** (`D2`).
- **Cabezales serie/I2C/UART/PWM** libres para módulos adicionales (Bluetooth, APC220, servos).

---

## 📌 Mapeo de Pines (Hardware Pinout)

| Componente | Pin Arduino | Modo | Notas / Lógica |
| :--- | :---: | :---: | :--- |
| **LED 1 (D1)** | `D13` | Digital OUT | Nivel `LOW` = Encendido |
| **LED 2 (D2)** | `D12` | Digital OUT | Nivel `LOW` = Encendido |
| **LED 3 (D3)** | `D11` | Digital OUT | Nivel `LOW` = Encendido |
| **LED 4 (D4)** | `D10` | Digital OUT | Nivel `LOW` = Encendido |
| **Botón 1 (S1)** | `A1` | Digital IN | `LOW` al pulsar (Pull-up interno) |
| **Botón 2 (S2)** | `A2` | Digital IN | `LOW` al pulsar (Pull-up interno) |
| **Botón 3 (S3)** | `A3` | Digital IN | `LOW` al pulsar (Pull-up interno) |
| **Buzzer** | `D3` | Digital OUT | Nivel `LOW` activa el tono |
| **Potenciómetro (POT)** | `A0` | Analog IN | Rango `0 – 1023` (0 a 5V) |
| **Display 74HC595 LATCH** | `D4` | Digital OUT | Pin de bloqueo (*RCK / ST_CP*) |
| **Display 74HC595 CLK** | `D7` | Digital OUT | Pin de reloj (*SCK / SH_CP*) |
| **Display 74HC595 DATA** | `D8` | Digital OUT | Pin de datos (*DIO / DS*) |
| **Sensor Temp (LM35/DS18B20)**| `A4` | Analog / 1-Wire | Jumper seleccionable |
| **Receptor IR** | `D2` | Digital IN | Interrupción externa INT0 |

---

## 📦 Instalación

### Método 1: Manual (Arduino IDE)
1. Descarga este repositorio como archivo `.zip` (`Code` -> `Download ZIP`).
2. En el Arduino IDE, ve a **Programa -> Incluir Librería -> Añadir biblioteca .ZIP...**
3. Selecciona el archivo descargado.

### Método 2: PlatformIO
Añade la ruta o dependencia en tu archivo `platformio.ini`:
```ini
[env:uno]
platform = atmelavr
board = uno
framework = arduino
lib_deps = 
    [https://github.com/LordLiberte/MFS-Arduino-Library.git](https://github.com/LordLiberte/MFS-Arduino-Library.git)
    TimerOne
