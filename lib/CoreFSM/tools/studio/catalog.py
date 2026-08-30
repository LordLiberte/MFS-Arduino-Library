"""Static catalog used by the Studio backend and its offline editor.

The browser never has to guess which methods a CoreFSM object exposes.  This
small, versioned catalog is shipped with the library and enriched with the
symbols that exist in the open project.
"""

from __future__ import annotations

import copy


BOARDS = {
    "nano": {
        "id": "nano",
        "label": "Arduino Nano",
        "platform": "atmelavr",
        "pioBoard": "nanoatmega328",
        "family": "AVR",
        "digitalPins": [str(value) for value in range(14)],
        "analogPins": ["A%d" % value for value in range(8)],
        "pwmPins": ["3", "5", "6", "9", "10", "11"],
        "note": "ATmega328P, 2 KB de RAM. Ideal para robots sencillos.",
    },
    "uno": {
        "id": "uno",
        "label": "Arduino Uno / Keyestudio UNO",
        "platform": "atmelavr",
        "pioBoard": "uno",
        "family": "AVR",
        "digitalPins": [str(value) for value in range(14)],
        "analogPins": ["A%d" % value for value in range(6)],
        "pwmPins": ["3", "5", "6", "9", "10", "11"],
        "note": "Compatible con la mayoría de shields Keyestudio basados en UNO.",
    },
    "mega": {
        "id": "mega",
        "label": "Arduino Mega 2560",
        "platform": "atmelavr",
        "pioBoard": "megaatmega2560",
        "family": "AVR",
        "digitalPins": [str(value) for value in range(54)],
        "analogPins": ["A%d" % value for value in range(16)],
        "pwmPins": ["2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "44", "45", "46"],
        "note": "Más E/S y memoria para máquinas con muchos dispositivos.",
    },
    "esp32": {
        "id": "esp32",
        "label": "ESP32 DevKit",
        "platform": "espressif32",
        "pioBoard": "esp32dev",
        "family": "ESP32",
        "digitalPins": [str(value) for value in (2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 39)],
        "analogPins": [str(value) for value in (32, 33, 34, 35, 36, 39)],
        "pwmPins": [str(value) for value in (2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33)],
        "note": "Más potencia, conectividad y RAM; GPIO 34-39 son solo entrada.",
    },
}


METHOD_CATALOG = {
    "DI": [
        {"label": "isTriggered()", "insert": "isTriggered()", "detail": "bool · nivel filtrado activo"},
        {"label": "isClear()", "insert": "isClear()", "detail": "bool · entrada inactiva"},
        {"label": "hasRisen()", "insert": "hasRisen()", "detail": "bool · flanco de activación de este scan"},
        {"label": "hasFallen()", "insert": "hasFallen()", "detail": "bool · flanco de liberación de este scan"},
        {"label": "rawValue()", "insert": "rawValue()", "detail": "bool · muestra sin antirrebote"},
        {"label": "timeInState()", "insert": "timeInState()", "detail": "ms · tiempo estable en el nivel actual"},
        {"label": "isStableFor(ms)", "insert": "isStableFor()", "detail": "bool · estable durante el tiempo indicado"},
        {"label": "force(value)", "insert": "force()", "detail": "Fuerza un valor lógico para pruebas"},
        {"label": "releaseForce()", "insert": "releaseForce()", "detail": "Libera el forzado de mantenimiento"},
    ],
    "DO": [
        {"label": "set(value)", "insert": "set()", "detail": "Escribe una orden lógica"},
        {"label": "turnOn()", "insert": "turnOn()", "detail": "Activa la salida"},
        {"label": "turnOff()", "insert": "turnOff()", "detail": "Desactiva la salida"},
        {"label": "toggle()", "insert": "toggle()", "detail": "Invierte la orden"},
        {"label": "setMode(mode)", "insert": "setMode()", "detail": "Fijo, parpadeo lento, rápido o flash"},
        {"label": "isOn()", "insert": "isOn()", "detail": "bool · hay una orden activa"},
        {"label": "isActive()", "insert": "isActive()", "detail": "bool · nivel físico aplicado"},
        {"label": "setMaxOnTime(ms)", "insert": "setMaxOnTime()", "detail": "Watchdog de activación continua"},
        {"label": "hasTimedOut()", "insert": "hasTimedOut()", "detail": "bool · watchdog enclavado"},
        {"label": "force(value)", "insert": "force()", "detail": "Fuerza la salida para mantenimiento"},
    ],
    "AI": [
        {"label": "raw()", "insert": "raw()", "detail": "uint16_t · lectura ADC sin filtrar"},
        {"label": "value()", "insert": "value()", "detail": "uint16_t · lectura filtrada"},
        {"label": "scaled()", "insert": "scaled()", "detail": "int32_t · valor en unidades de ingeniería"},
        {"label": "setScale(rawMin, rawMax, engMin, engMax)", "insert": "setScale()", "detail": "Configura el escalado lineal"},
        {"label": "setFilter(alpha)", "insert": "setFilter()", "detail": "Ajusta el filtro EMA"},
        {"label": "setThreshold(on, off)", "insert": "setThreshold()", "detail": "Umbral con histéresis"},
        {"label": "threshold()", "insert": "threshold()", "detail": "bool · estado del umbral"},
        {"label": "isValid()", "insert": "isValid()", "detail": "bool · medida dentro de rango"},
        {"label": "force(rawValue)", "insert": "force()", "detail": "Fuerza una lectura para pruebas"},
    ],
    "servo": [
        {"label": "write(angulo)", "insert": "write()", "detail": "Ordena un ángulo de 0 a 180 grados"},
        {"label": "read()", "insert": "read()", "detail": "uint8_t · último ángulo ordenado"},
        {"label": "attach(pin)", "insert": "attach()", "detail": "Asocia el servo a su pin (lo hace ProjectDevicesBegin)"},
        {"label": "detach()", "insert": "detach()", "detail": "Libera el pin y deja de generar pulsos"},
    ],
    "SequenceBlock": [
        {"label": "setStep(step)", "insert": "setStep()", "detail": "Cambia de paso y reinicia su tiempo"},
        {"label": "setStep(step, timeoutMs)", "insert": "setStep(, )", "detail": "Cambia de paso con vigilancia"},
        {"label": "getTimeInStep()", "insert": "getTimeInStep()", "detail": "ms · tiempo desde la entrada al paso"},
        {"label": "completeCycle(step)", "insert": "completeCycle()", "detail": "Cierra ciclo, cuenta y cambia de paso"},
        {"label": "updateSequence()", "insert": "updateSequence()", "detail": "Aplica mando, pausa, fallo y timeouts"},
        {"label": "requestStop()", "insert": "requestStop()", "detail": "Solicita una parada ordenada"},
        {"label": "fault(code)", "insert": "fault()", "detail": "Lleva el bloque a fallo con código"},
        {"label": "isFaulted()", "insert": "isFaulted()", "detail": "bool · bloque en fallo"},
        {"label": "getStep()", "insert": "getStep()", "detail": "uint16_t · paso actual"},
        {"label": "suspendWhile(cond)", "insert": "suspendWhile()", "detail": "bool · espera por causa externa; congela los cronómetros"},
        {"label": "holdWhile(cond)", "insert": "holdWhile()", "detail": "bool · espera por causa interna o del operario"},
        {"label": "getTimeInStep()", "insert": "getTimeInStep()", "detail": "ms · tiempo en el paso, sin contar esperas"},
        {"label": "getBlockedTime()", "insert": "getBlockedTime()", "detail": "ms · tiempo total esperando. Es el dato del OEE"},
        {"label": "setCycleTimeout(ms)", "insert": "setCycleTimeout()", "detail": "Límite duro del ciclo; al superarlo, alarma"},
        {"label": "setCycleTarget(ms)", "insert": "setCycleTarget()", "detail": "Takt objetivo; solo avisa"},
        {"label": "setInitialStep(paso)", "insert": "setInitialStep()", "detail": "Paso al que vuelve tras un rearme"},
        {"label": "setName(F(\"\"))", "insert": "setName(F(\"\"))", "detail": "Nombre del bloque para la telemetría"},
    ],
    "chassis": [
        {"label": "forward(speed)", "insert": "forward()", "detail": "Avanza a velocidad 0…255"},
        {"label": "backward(speed)", "insert": "backward()", "detail": "Retrocede"},
        {"label": "spinLeft(speed)", "insert": "spinLeft()", "detail": "Pivota a la izquierda"},
        {"label": "spinRight(speed)", "insert": "spinRight()", "detail": "Pivota a la derecha"},
        {"label": "drive(v, w)", "insert": "drive(, )", "detail": "Mando diferencial de avance y giro"},
        {"label": "stop()", "insert": "stop()", "detail": "Ordena velocidad cero"},
        {"label": "enable()", "insert": "enable()", "detail": "Habilita los drivers"},
        {"label": "disable()", "insert": "disable()", "detail": "Deshabilita los drivers"},
    ],
    "motor_drive": [
        {"label": "setSpeed(speed)", "insert": "setSpeed()", "detail": "Velocidad con sentido actual"},
        {"label": "setSignedSpeed(speed)", "insert": "setSignedSpeed()", "detail": "Velocidad -255…255"},
        {"label": "forward(speed)", "insert": "forward()", "detail": "Giro hacia delante"},
        {"label": "backward(speed)", "insert": "backward()", "detail": "Giro hacia atrás"},
        {"label": "stop()", "insert": "stop()", "detail": "Frena el motor"},
        {"label": "setRamp(step)", "insert": "setRamp()", "detail": "Rampa PWM por milisegundo"},
        {"label": "enable()", "insert": "enable()", "detail": "Habilita el driver"},
        {"label": "disable()", "insert": "disable()", "detail": "Deshabilita el driver"},
    ],
    "ultrasonic": [
        {"label": "cm()", "insert": "cm()", "detail": "uint16_t · distancia filtrada en cm"},
        {"label": "isClear(threshold)", "insert": "isClear()", "detail": "bool · zona libre"},
        {"label": "isObstacle(threshold)", "insert": "isObstacle()", "detail": "bool · obstáculo dentro del umbral"},
        {"label": "hasEcho()", "insert": "hasEcho()", "detail": "bool · existe eco válido"},
        {"label": "force(distanceCm)", "insert": "force()", "detail": "Fuerza una distancia de prueba"},
    ],
}


def _starter_manifest():
    return {
        "schemaVersion": 1,
        "project": {
            "displayName": "Nueva máquina",
            "preset": "starter",
            "description": "Proyecto CoreFSM guiado",
        },
        "hardware": [
            {"name": "Pulsador_Marcha", "role": "DI", "target": "2", "pullup": True,
             "activeLow": "", "debounceMs": 20, "filter": "", "safe": "",
             "group": "Mando", "description": "Orden de inicio"},
            {"name": "Piloto_Trabajo", "role": "DO", "target": "13", "pullup": "",
             "activeLow": False, "debounceMs": "", "filter": "", "safe": False,
             "group": "Señalización", "description": "Indicador de ciclo"},
        ],
        "devices": [],
        "udts": [],
        "dataBlocks": [
            {"name": "DB_Proceso", "retained": False, "version": 1, "address": 0,
             "description": "Parámetros y datos del proceso",
             "variables": [
                 {"name": "tiempoTrabajoMs", "type": "uint16_t", "initial": "1500", "comment": "Duración del trabajo"},
                 {"name": "contadorCiclos", "type": "uint32_t", "initial": "0", "comment": "Ciclos completados"},
             ]},
        ],
        "states": [
            {"id": 0, "symbol": "PASO_REPOSO", "label": "Reposo", "description": "Esperando orden de marcha"},
            {"id": 10, "symbol": "PASO_TRABAJO", "label": "Trabajo", "description": "Proceso activo"},
        ],
        "transitions": [
            {"from": "PASO_REPOSO", "to": "PASO_TRABAJO", "condition": "ordenMarcha"},
            {"from": "PASO_TRABAJO", "to": "PASO_REPOSO", "condition": "tiempo cumplido"},
        ],
        "workspace": {"activeFile": "src/Proceso.h", "completedSteps": []},
    }


def _keyestudio_ks0192_manifest():
    data = _starter_manifest()
    data["project"] = {
        "displayName": "Keyestudio 4WD KS0192",
        "preset": "keyestudio_ks0192",
        "description": "UNO + L298N, perfil editable basado en el cableado oficial KS0192",
    }
    data["hardware"] = []
    data["devices"] = [
        {"name": "MotorIzquierdo", "kind": "motor_drive", "label": "Motores del lado izquierdo",
         "pins": {"in1": "2", "in2": "4", "pwm": "5"}, "options": {"ramp": 3}},
        {"name": "MotorDerecho", "kind": "motor_drive", "label": "Motores del lado derecho",
         "pins": {"in1": "7", "in2": "8", "pwm": "10"}, "options": {"ramp": 3}},
        {"name": "Sonar", "kind": "ultrasonic", "label": "HC-SR04 frontal",
         "pins": {"trig": "A1", "echo": "A0"}, "options": {"intervalMs": 60, "timeoutUs": 12000}},
        {"name": "ServoSonar", "kind": "servo", "label": "Servo de orientación",
         "pins": {"signal": "3"}, "options": {"initialAngle": 90}},
        {"name": "Chasis", "kind": "chassis_diff", "label": "Chasis diferencial 4WD",
         "refs": {"left": "MotorIzquierdo", "right": "MotorDerecho"}, "pins": {}, "options": {"trackWidth": 10}},
    ]
    data["dataBlocks"] = [
        {"name": "DB_Robot", "retained": False, "version": 1, "address": 0,
         "description": "Ajustes de navegación y telemetría",
         "variables": [
             {"name": "distanciaCritica", "type": "uint16_t", "initial": "25", "comment": "Umbral de obstáculo en cm"},
             {"name": "velocidadCrucero", "type": "uint8_t", "initial": "170", "comment": "PWM de avance"},
             {"name": "velocidadGiro", "type": "uint8_t", "initial": "190", "comment": "PWM de giro"},
             {"name": "msPivote", "type": "uint16_t", "initial": "350", "comment": "Tiempo para inspeccionar un lado"},
             {"name": "msGiro90", "type": "uint16_t", "initial": "420", "comment": "Calibración de giro de 90°"},
             {"name": "distanciaActual", "type": "uint16_t", "initial": "999", "comment": "Última medida del sonar"},
        ]},
    ]
    data["states"] = [
        {"id": 0, "symbol": "ROB_PARADO", "label": "Parado", "description": "Motores deshabilitados"},
        {"id": 10, "symbol": "ROB_EXPLORAR", "label": "Explorar", "description": "Avance con vigilancia frontal"},
        {"id": 20, "symbol": "ROB_FRENAR", "label": "Frenar", "description": "Estabiliza el robot antes de medir"},
        {"id": 30, "symbol": "ROB_MIRAR_IZQ", "label": "Mirar izquierda", "description": "Pivota y toma distancia"},
        {"id": 40, "symbol": "ROB_MIRAR_DER", "label": "Mirar derecha", "description": "Pivota y toma distancia"},
        {"id": 50, "symbol": "ROB_DECIDIR", "label": "Decidir", "description": "Elige el lado más despejado"},
        {"id": 60, "symbol": "ROB_GIRAR", "label": "Girar", "description": "Ejecuta la maniobra elegida"},
        {"id": 70, "symbol": "ROB_ESCAPAR", "label": "Escapar", "description": "Retrocede si ambos lados están cerrados"},
    ]
    data["transitions"] = [
        {"from": "ROB_PARADO", "to": "ROB_EXPLORAR", "condition": "ordenMarcha"},
        {"from": "ROB_EXPLORAR", "to": "ROB_FRENAR", "condition": "distanciaActual <= distanciaCritica"},
        {"from": "ROB_FRENAR", "to": "ROB_MIRAR_IZQ", "condition": "robot detenido"},
        {"from": "ROB_MIRAR_IZQ", "to": "ROB_MIRAR_DER", "condition": "medida izquierda lista"},
        {"from": "ROB_MIRAR_DER", "to": "ROB_DECIDIR", "condition": "medida derecha lista"},
        {"from": "ROB_DECIDIR", "to": "ROB_GIRAR", "condition": "hay una dirección libre"},
        {"from": "ROB_DECIDIR", "to": "ROB_ESCAPAR", "condition": "ambos lados bloqueados"},
        {"from": "ROB_GIRAR", "to": "ROB_EXPLORAR", "condition": "giro terminado"},
        {"from": "ROB_ESCAPAR", "to": "ROB_EXPLORAR", "condition": "maniobra terminada"},
    ]
    return data


def _reference_4wd_manifest():
    data = _keyestudio_ks0192_manifest()
    data["project"] = {
        "displayName": "Robot 4WD · cableado CoreFSM",
        "preset": "robot_4wd_reference",
        "description": "Nano y cuatro canales independientes, como el ejemplo 06",
    }
    data["hardware"] = [
        {"name": "Pulsador_Marcha", "role": "DI", "target": "A0", "pullup": True,
         "activeLow": "", "debounceMs": 30, "filter": "", "safe": "", "group": "Mando", "description": "Marcha/paro"},
        {"name": "Led_Estado", "role": "DO", "target": "A1", "pullup": "",
         "activeLow": False, "debounceMs": "", "filter": "", "safe": False, "group": "Señalización", "description": "Estado del robot"},
    ]
    data["devices"] = [
        {"name": "RuedaFL", "kind": "motor_drive", "label": "Delantera izquierda", "pins": {"in1": "2", "in2": "4", "pwm": "3"}, "options": {"ramp": 3}},
        {"name": "RuedaFR", "kind": "motor_drive", "label": "Delantera derecha", "pins": {"in1": "7", "in2": "8", "pwm": "5"}, "options": {"ramp": 3}},
        {"name": "RuedaRL", "kind": "motor_drive", "label": "Trasera izquierda", "pins": {"in1": "12", "in2": "13", "pwm": "6"}, "options": {"ramp": 3}},
        {"name": "RuedaRR", "kind": "motor_drive", "label": "Trasera derecha", "pins": {"in1": "A2", "in2": "A3", "pwm": "9"}, "options": {"ramp": 3}},
        {"name": "Sonar", "kind": "ultrasonic", "label": "HC-SR04 frontal", "pins": {"trig": "10", "echo": "11"}, "options": {"intervalMs": 60, "timeoutUs": 12000}},
        {"name": "Chasis", "kind": "chassis_4wd", "label": "Chasis de cuatro ruedas", "refs": {"fl": "RuedaFL", "fr": "RuedaFR", "rl": "RuedaRL", "rr": "RuedaRR"}, "pins": {}, "options": {}},
    ]
    return data


PRESETS = {
    "starter": {
        "id": "starter", "label": "Máquina básica", "board": "nano",
        "category": "Inicio", "description": "Pulsador, piloto, DB y secuencia de dos pasos.",
        "icon": "spark", "manifest": _starter_manifest,
    },
    "keyestudio_ks0192": {
        "id": "keyestudio_ks0192", "label": "Keyestudio 4WD KS0192", "board": "uno",
        "category": "Robot", "description": "UNO + L298N + HC-SR04 + servo; pines editables.",
        "icon": "robot", "manifest": _keyestudio_ks0192_manifest,
    },
    "robot_4wd_reference": {
        "id": "robot_4wd_reference", "label": "4WD CoreFSM de referencia", "board": "nano",
        "category": "Robot", "description": "Cuatro motores independientes y sonar del ejemplo 06.",
        "icon": "wheels", "manifest": _reference_4wd_manifest,
    },
    "empty": {
        "id": "empty", "label": "Proyecto vacío", "board": "nano",
        "category": "Avanzado", "description": "Solo la estructura mínima, sin decisiones previas.",
        "icon": "blank", "manifest": lambda: {
            "schemaVersion": 1,
            "project": {"displayName": "Proyecto vacío", "preset": "empty", "description": ""},
            "hardware": [], "devices": [], "udts": [], "dataBlocks": [],
            "states": [{"id": 0, "symbol": "PASO_REPOSO", "label": "Reposo", "description": ""}],
            "transitions": [], "workspace": {"activeFile": "src/Proceso.h", "completedSteps": []},
        },
    },
}


DEVICE_TYPES = {
    "motor_drive": {"label": "Motor DC / puente H", "pins": ["in1", "in2", "pwm"], "methodGroup": "motor_drive"},
    "ultrasonic": {"label": "Sonar HC-SR04", "pins": ["trig", "echo"], "methodGroup": "ultrasonic"},
    "servo": {"label": "Servo", "pins": ["signal"], "methodGroup": "servo"},
    "chassis_diff": {"label": "Chasis diferencial", "pins": [], "methodGroup": "chassis"},
    "chassis_4wd": {"label": "Chasis 4 ruedas", "pins": [], "methodGroup": "chassis"},
}


def public_catalog():
    presets = []
    for preset in PRESETS.values():
        presets.append({key: value for key, value in preset.items() if key != "manifest"})
    return {
        "boards": copy.deepcopy(list(BOARDS.values())),
        "presets": copy.deepcopy(presets),
        "deviceTypes": copy.deepcopy(DEVICE_TYPES),
        "methods": copy.deepcopy(METHOD_CATALOG),
        "dataTypes": ["bool", "int8_t", "uint8_t", "int16_t", "uint16_t", "int32_t", "uint32_t", "float"],
        "roleFields": {"DI": ["pullup", "debounceMs"], "DO": ["activeLow", "safe"], "AI": ["filter"]},
    }


def manifest_for_preset(preset_id, display_name=None):
    if preset_id not in PRESETS:
        raise KeyError(preset_id)
    manifest = copy.deepcopy(PRESETS[preset_id]["manifest"]())
    if display_name:
        manifest["project"]["displayName"] = display_name
    return manifest
