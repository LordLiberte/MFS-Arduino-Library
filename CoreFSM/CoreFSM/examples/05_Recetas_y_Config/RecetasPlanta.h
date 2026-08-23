#ifndef RECETAS_PLANTA_H
#define RECETAS_PLANTA_H

/* ===========================================================================
 *  RecetasPlanta.h  -  Catalogo de recetas de fabrica
 * ---------------------------------------------------------------------------
 *  IMPORTANTE: estos dos #define tienen que ir ANTES de incluir CoreFSM,
 *  porque determinan el tamano de las estructuras de receta y por tanto la RAM
 *  que ocupan. Ajustalos a tu maquina y no te pases: cada eje y cada paso de
 *  mas se pagan en memoria aunque no los uses.
 *
 *  Con 2 ejes y 4 pasos, la receta activa ocupa unos 90 bytes en un Nano.
 *  Con 3 ejes y 8 pasos serian unos 260, un 13% de toda la RAM disponible.
 * ======================================================================== */
#define CFSM_RECIPE_AXES      2
#define CFSM_RECIPE_MAX_STEPS 4
#define CFSM_RECIPE_NAME_LEN  16    /* caben nombres como "MANTENIMIENTO" */

#include <CoreFSM.h>

/* ---------------------------------------------------------------------------
 *  Significado de los bits de la mascara de herramienta en ESTA maquina.
 *  El ejecutor de recetas no los interpreta: solo los transporta. Quien les da
 *  significado es applyTool(), mas abajo.
 * ------------------------------------------------------------------------ */
#define HERR_PINZA    0x01
#define HERR_VENTOSA  0x02

/* ===========================================================================
 *  Las recetas de fabrica, en memoria de programa (CFSM_PROGMEM).
 * ---------------------------------------------------------------------------
 *  Van en flash, no en RAM. En un Nano hay 30 KB de flash libre: caben mas de
 *  cien recetas. En RAM solo cabe una, y por eso solo se copia la activa.
 *
 *  Formato de cada eje:  { destino, velocidad, tolerancia, activo }
 *  Formato de la herramienta: { mascara, ms_de_asentamiento, exige_confirmacion }
 *  Formato de la transicion:  { criterio, ms_permanencia, ms_timeout, id_sensor }
 * ======================================================================== */
const RecipeRecord RECETAS_FABRICA[] CFSM_PROGMEM = {

  /* ---------------- Receta 0: pieza pequena ---------------- */
  { { 1, "PIEZA_CHICA", 4, 30000 },
    {
      /* Paso 0: aproximacion en alto, pinza abierta */
      { { {200, 200,  6, true}, { 40, 180, 6, true} },
        { 0,            100, false },
        { TRIG_POSITION, 150, 6000, 0 } },

      /* Paso 1: bajar y agarrar. Exige confirmacion: si la pinza no detecta
         pieza, seguir con el ciclo seria mover el aire. */
      { { {200, 120,  4, true}, {180, 120, 4, true} },
        { HERR_PINZA,   400, true  },
        { TRIG_POSITION, 200, 7000, 0 } },

      /* Paso 2: elevar con la pieza agarrada */
      { { {200, 180,  6, true}, { 40, 160, 6, true} },
        { HERR_PINZA,   100, false },
        { TRIG_POSITION, 100, 6000, 0 } },

      /* Paso 3: llevar al palet y soltar */
      { { {620, 200,  8, true}, { 90, 170, 6, true} },
        { 0,            300, false },
        { TRIG_POSITION, 200, 9000, 0 } }
    }
  },

  /* ---------------- Receta 1: pieza grande ----------------
     Misma secuencia, otras coordenadas y tiempos mas largos. Fijate en que
     NO hay ni una linea de codigo distinta: solo datos. Eso es exactamente lo
     que se gana al separar receta de programa. */
  { { 2, "PIEZA_GRANDE", 4, 40000 },
    {
      { { {180, 170,  8, true}, { 30, 150, 8, true} },
        { 0,            150, false },
        { TRIG_POSITION, 200, 8000, 0 } },
      { { {180, 100,  5, true}, {210, 100, 5, true} },
        { HERR_PINZA,   700, true  },
        { TRIG_POSITION, 300, 9000, 0 } },
      { { {180, 150,  8, true}, { 30, 140, 8, true} },
        { HERR_PINZA,   150, false },
        { TRIG_POSITION, 150, 8000, 0 } },
      { { {700, 170, 10, true}, {110, 150, 8, true} },
        { 0,            400, false },
        { TRIG_POSITION, 300, 12000, 0 } }
    }
  },

  /* ---------------- Receta 2: rutina de mantenimiento ----------------
     Solo dos pasos: llevar los ejes a la posicion de servicio y quedarse. */
  { { 99, "MANTENIMIENTO", 2, 20000 },
    {
      { { {  0, 120,  5, true}, {  0, 120, 5, true} },
        { 0,            0, false },
        { TRIG_POSITION, 500, 15000, 0 } },
      { { {512, 100, 10, true}, {512, 100,10, true} },
        { 0,            0, false },
        { TRIG_TIMER,   3000,  8000, 0 } }
    }
  }
};

const uint8_t NUM_RECETAS_FABRICA =
  sizeof(RECETAS_FABRICA) / sizeof(RECETAS_FABRICA[0]);

/* ===========================================================================
 *  Configuracion de maquina: lo que se ajusta en la puesta en marcha y casi
 *  nunca despues. Va en EEPROM, separada de las recetas.
 * ======================================================================== */
struct ConfigMaquina {
  uint16_t velocidadMaxima   = 220;   /* limite global de velocidad         */
  uint16_t offsetEjeX        = 0;     /* calibracion mecanica del eje X     */
  uint16_t offsetEjeY        = 0;
  uint16_t recetaPorDefecto  = 1;     /* que receta cargar al arrancar      */
  uint32_t piezasTotales     = 0;     /* contador de vida de la maquina     */
  uint32_t horasFuncionamiento = 0;
};

#endif
