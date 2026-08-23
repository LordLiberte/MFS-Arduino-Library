#ifndef COREFSM_RECIPE_TYPES_H
#define COREFSM_RECIPE_TYPES_H

#include "../core/CoreFSM_Platform.h"
#include "ConfigStore.h"

/* ===========================================================================
 *  RecipeTypes.h  -  Estructura de datos de una receta
 * ---------------------------------------------------------------------------
 *  QUE ES UNA RECETA Y POR QUE CAMBIA LA FORMA DE PROGRAMAR
 *  --------------------------------------------------------
 *  Sin recetas, la secuencia y los datos estan mezclados:
 *
 *      case PASO_BAJAR:  eje.moveTo(120); if (...) setStep(PASO_SOLDAR); break;
 *      case PASO_SOLDAR: if (getTimeInStep() >= 2000) ...
 *
 *  El 120 y el 2000 estan clavados en el codigo. Cambiar de producto obliga a
 *  recompilar y volver a cargar el firmware, y cada modelo distinto necesita
 *  su propia version del programa. Es exactamente lo que hace inmanejable una
 *  maquina que fabrica mas de una referencia.
 *
 *  Con recetas, el programa deja de contener numeros:
 *
 *      RECETA "PIEZA_A"          RECETA "PIEZA_B"
 *      paso1: eje=120 t=2000     paso1: eje=95  t=3500
 *      paso2: eje=40  t=500      paso2: eje=40  t=800
 *
 *  Y el codigo se convierte en un interprete generico: "lee el paso actual de
 *  la receta activa, mueve los ejes a donde diga, espera lo que diga, pasa al
 *  siguiente". Un unico programa fabrica cualquier producto, y anadir una
 *  referencia nueva es anadir datos, no codigo. Esta es la diferencia entre
 *  una maquina de un solo producto y una maquina de verdad.
 *
 *  DONDE VIVEN LAS RECETAS
 *  -----------------------
 *  Una receta de 8 pasos y 3 ejes ocupa unos 250 bytes. En un Arduino Nano con
 *  2 KB de RAM no caben mas de dos o tres, asi que se hace lo mismo que en una
 *  maquina real:
 *
 *      FLASH (PROGMEM)  Todas las recetas de fabrica. En un Nano hay 30 KB
 *                       libres: caben mas de cien. No se pueden modificar.
 *      EEPROM           Las recetas que el operario ha creado o retocado
 *                       mediante aprendizaje (teach-in). Se conservan al
 *                       apagar y se pueden reescribir.
 *      RAM              SOLO la receta activa, una sola. Es la que ejecuta la
 *                       maquina en este momento.
 *
 *  Cargar una receta = copiar de flash o de EEPROM al hueco de RAM. Es lo que
 *  hace un centro de mecanizado cuando seleccionas un programa de pieza.
 * ======================================================================== */

/* Ajusta estos dos numeros a tu maquina ANTES de incluir la libreria.
 * Afectan directamente al tamano de la receta y por tanto a la RAM que ocupa. */
#ifndef CFSM_RECIPE_AXES
  #define CFSM_RECIPE_AXES 3
#endif
#ifndef CFSM_RECIPE_MAX_STEPS
  #define CFSM_RECIPE_MAX_STEPS 8
#endif
#ifndef CFSM_RECIPE_NAME_LEN
  #define CFSM_RECIPE_NAME_LEN 12
#endif

/* ---------------------------------------------------------------------------
 *  Consigna de un eje dentro de un paso
 * ------------------------------------------------------------------------ */
/* AVISO SOBRE EL ESTILO DE ESTAS ESTRUCTURAS
 * Ninguna lleva inicializadores por defecto en sus miembros (nada de "= 0").
 * Hay dos motivos, y los dos importan:
 *
 *   1. En C++11 -que es el dialecto que usa el compilador de Arduino AVR- una
 *      estructura con inicializadores de miembro DEJA DE SER un agregado, y
 *      entonces ya no se puede escribir la tabla de recetas con llaves:
 *          { {200, 180, 6, true}, ... }   <- fallaria al compilar
 *      Sin ellos, la sintaxis de tabla funciona y las recetas se leen como una
 *      hoja de calculo, que es justo lo que se busca.
 *
 *   2. Estas estructuras se guardan en memoria no volatil copiando bytes en
 *      crudo. Cuanto mas simples y predecibles sean (POD puro), menos
 *      sorpresas al escribirlas y releerlas.
 *
 * A cambio, una variable suelta de estos tipos nace con basura. Usa siempre
 * inicializacion con llaves, o cfsmClearRecipe() para dejarla a cero. */

struct AxisMotion {
  int16_t target;      /* posicion de destino, en las unidades del eje */
  uint8_t speed;       /* velocidad de aproximacion (0..255)           */
  int16_t tolerance;   /* ventana de llegada                           */
  bool    enabled;     /* false = este paso no mueve este eje          */
};

/* ---------------------------------------------------------------------------
 *  Accion sobre la herramienta / actuadores del paso
 * ---------------------------------------------------------------------------
 *  Se usa una mascara de bits en vez de booleanos con nombre para que la
 *  estructura no crezca con cada actuador nuevo: ocho salidas caben en un solo
 *  byte. El significado de cada bit lo decides tu (bit 0 = pinza, bit 1 =
 *  vacio, bit 2 = soldador...) y lo interpreta tu maquina.
 * ------------------------------------------------------------------------ */
struct ToolAction {
  uint8_t  outputs;         /* mascara de salidas a activar               */
  uint16_t settleMs;        /* espera tras accionar (cierre mecanico)     */
  bool     requireFeedback; /* exigir confirmacion por sensor              */
};

/* ---------------------------------------------------------------------------
 *  Condicion de salto al paso siguiente
 * ------------------------------------------------------------------------ */
enum StepTrigger : uint8_t {
  TRIG_POSITION      = 0,  /* cuando todos los ejes activos han llegado      */
  TRIG_TIMER         = 1,  /* cuando pasa el tiempo de permanencia           */
  TRIG_SENSOR        = 2,  /* cuando se activa el sensor indicado            */
  TRIG_POSITION_TIME = 3,  /* llegada Y ADEMAS tiempo cumplido               */
  TRIG_MANUAL        = 4   /* solo con orden externa (paso a paso)           */
};

struct StepTransition {
  StepTrigger trigger;    /* criterio de salto al paso siguiente          */
  uint16_t    dwellMs;    /* permanencia en destino antes de saltar       */
  uint16_t    timeoutMs;  /* vigilancia: 0 desactiva (no recomendado)     */
  uint8_t     sensorId;   /* cual de tus sensores mira TRIG_SENSOR        */
};

/* ---------------------------------------------------------------------------
 *  Un paso completo
 * ------------------------------------------------------------------------ */
struct RecipeStep {
  AxisMotion     axis[CFSM_RECIPE_AXES];
  ToolAction     tool;
  StepTransition transition;
};

/* ---------------------------------------------------------------------------
 *  Cabecera y receta completa
 * ------------------------------------------------------------------------ */
struct RecipeHeader {
  uint16_t id;                          /* identificador unico           */
  char     name[CFSM_RECIPE_NAME_LEN];  /* nombre del producto (SKU)     */
  uint8_t  totalSteps;                  /* pasos realmente usados        */
  uint32_t maxCycleMs;                  /* vigilancia del ciclo completo */
};

struct RecipeRecord {
  RecipeHeader header;
  RecipeStep   steps[CFSM_RECIPE_MAX_STEPS];
};

/* Deja una receta a cero. Usalo antes de construir una a mano en RAM o antes
 * de empezar un aprendizaje desde cero. */
inline void cfsmClearRecipe(RecipeRecord& r) {
  memset(&r, 0, sizeof(RecipeRecord));
}

/* ===========================================================================
 *  RecipeBank  -  Catalogo de recetas
 * ---------------------------------------------------------------------------
 *  Gestiona los tres almacenes (flash, EEPROM, RAM) tras una interfaz unica.
 *
 *  Las recetas de fabrica se declaran en flash con la macro CFSM_RECIPE_TABLE
 *  y NO ocupan RAM. Se copian a 'active' solo al seleccionarlas.
 * ======================================================================== */

template <uint8_t NVM_SLOTS = 4, uint16_t NVM_BASE = 256>
class RecipeBank {
  public:
    /* La unica receta que vive en RAM: la que se esta ejecutando. */
    RecipeRecord active;

    RecipeBank() : _factory(nullptr), _factoryCount(0), _activeIndex(0xFF) {
      /* Sin inicializadores por defecto en las estructuras, hay que dejar la
       * receta activa a cero expresamente: una receta con basura podria decir
       * que tiene 200 pasos y provocar una lectura fuera del array. */
      cfsmClearRecipe(active);
    }

    /* Registra la tabla de recetas de fabrica que vive en memoria de programa.
     *
     *   const RecipeRecord RECETAS[] CFSM_PROGMEM = { ... };
     *   bank.setFactoryTable(RECETAS, 3);
     */
    void setFactoryTable(const RecipeRecord* table, uint8_t count) {
      _factory      = table;
      _factoryCount = count;
    }

    uint8_t factoryCount() const { return _factoryCount; }

    /* --- Carga desde flash ------------------------------------------------
     * En AVR hay que copiar explicitamente desde memoria de programa con
     * memcpy_P: en esa arquitectura la flash y la RAM son espacios de
     * direcciones DISTINTOS y un puntero normal no puede leer de la flash.
     * En ESP32 y compania el espacio es unico y basta con copiar. */
    bool loadFactory(uint8_t index) {
      if (!_factory || index >= _factoryCount) return false;
    #if defined(CFSM_ARCH_AVR)
      memcpy_P(&active, &_factory[index], sizeof(RecipeRecord));
    #else
      memcpy(&active, &_factory[index], sizeof(RecipeRecord));
    #endif
      _activeIndex = index;
      _activeFromNvm = false;
      return validateActive();
    }

    /* Busca por identificador en lugar de por posicion. */
    bool loadFactoryById(uint16_t id) {
      for (uint8_t i = 0; i < _factoryCount; i++) {
        uint16_t candidate;
      #if defined(CFSM_ARCH_AVR)
        memcpy_P(&candidate, &_factory[i].header.id, sizeof(uint16_t));
      #else
        candidate = _factory[i].header.id;
      #endif
        if (candidate == id) return loadFactory(i);
      }
      return false;
    }

    /* --- Ranuras de usuario en memoria no volatil ------------------------- */

    bool loadSlot(uint8_t slot) {
    #if !CFSM_HAS_NVM
      CFSM_UNUSED(slot); return false;
    #else
      if (!slotFits(slot)) return false;
      uint16_t addr = slotAddress(slot);
      CfsmStoreHeader h;
      for (uint16_t i = 0; i < sizeof(h); i++) ((uint8_t*)&h)[i] = EEPROM.read(addr + i);
      if (h.magic != CFSM_STORE_MAGIC || h.size != sizeof(RecipeRecord)) return false;

      RecipeRecord tmp;
      for (uint16_t i = 0; i < sizeof(tmp); i++)
        ((uint8_t*)&tmp)[i] = EEPROM.read(addr + sizeof(h) + i);

      if (cfsmCrc16((const uint8_t*)&tmp, sizeof(tmp)) != h.crc) return false;

      active = tmp;
      _activeIndex   = slot;
      _activeFromNvm = true;
      return validateActive();
    #endif
    }

    /* Guarda la receta activa en una ranura. Es la operacion de "grabar" tras
     * un aprendizaje por teach-in. */
    bool saveSlot(uint8_t slot) {
    #if !CFSM_HAS_NVM
      CFSM_UNUSED(slot); return false;
    #else
      if (!slotFits(slot)) return false;
      uint16_t addr = slotAddress(slot);
      CfsmStoreHeader h;
      h.magic = CFSM_STORE_MAGIC;
      h.version = 1;
      h.size = sizeof(RecipeRecord);
      h.crc  = cfsmCrc16((const uint8_t*)&active, sizeof(active));

      for (uint16_t i = 0; i < sizeof(h); i++) {
        uint8_t b = ((const uint8_t*)&h)[i];
        if (EEPROM.read(addr + i) != b) EEPROM.write(addr + i, b);
      }
      for (uint16_t i = 0; i < sizeof(active); i++) {
        uint8_t b = ((const uint8_t*)&active)[i];
        uint16_t a = addr + sizeof(h) + i;
        if (EEPROM.read(a) != b) EEPROM.write(a, b);
      }
    #if CFSM_NVM_NEEDS_COMMIT
      EEPROM.commit();
    #endif
      return true;
    #endif
    }

    /* --- Estado ---------------------------------------------------------- */
    bool     hasActive()   const { return active.header.totalSteps > 0; }
    uint8_t  activeIndex() const { return _activeIndex; }
    bool     activeIsFromNvm() const { return _activeFromNvm; }
    const char* activeName() const { return active.header.name; }

    /* Comprueba que la receta cargada tiene sentido. Una receta corrupta o mal
     * escrita a mano puede pedir 200 pasos de un array de 8 y provocar una
     * lectura fuera de rango; en un microcontrolador eso no da excepcion, da
     * comportamiento aleatorio. Mas vale rechazarla. */
    bool validateActive() {
      if (active.header.totalSteps == 0) return false;
      if (active.header.totalSteps > CFSM_RECIPE_MAX_STEPS) {
        active.header.totalSteps = 0;
        return false;
      }
      return true;
    }

    /* Espacio que ocupan todas las ranuras, para no solaparlo con otros datos.
     *
     * Compruebalo contra la EEPROM real de tu placa ANTES de dar por buena la
     * configuracion. Una receta de 3 ejes y 8 pasos ocupa unos 250 bytes, asi
     * que en el 1 KB de un ATmega328P caben tres ranuras contando la
     * configuracion de maquina. loadSlot() y saveSlot() rechazan las que no
     * quepan en lugar de escribir fuera, pero es mejor dimensionarlo bien. */
    static uint16_t nvmFootprint() {
      return NVM_SLOTS * (sizeof(CfsmStoreHeader) + sizeof(RecipeRecord));
    }
    static uint16_t nvmEndAddress() { return NVM_BASE + nvmFootprint(); }

    /* Cuantas ranuras caben de verdad en esta placa. */
    static uint8_t usableSlots() {
      uint8_t n = 0;
      for (uint8_t i = 0; i < NVM_SLOTS; i++) if (slotFits(i)) n++;
      return n;
    }

  private:
    const RecipeRecord* _factory;
    uint8_t             _factoryCount;
    uint8_t             _activeIndex;
    bool                _activeFromNvm = false;

    static uint16_t slotAddress(uint8_t slot) {
      return NVM_BASE + slot * (uint16_t)(sizeof(CfsmStoreHeader) + sizeof(RecipeRecord));
    }

    /* Comprueba que la ranura CABE de verdad en la memoria no volatil.
     *
     * No es una comprobacion academica. En un ATmega328P la EEPROM son 1024
     * bytes y su registro de direcciones es de 10 bits: una direccion de 1100
     * no da error, DA LA VUELTA y escribe en la 76. Con los valores por
     * defecto de esta plantilla y una receta de 3 ejes y 8 pasos, la cuarta
     * ranura ya se sale, y al grabarla machacaria la configuracion de maquina
     * que vive al principio. El sintoma seria "al arrancar, la configuracion
     * sale corrupta" sin ninguna relacion aparente con haber guardado una
     * receta. */
    static bool slotFits(uint8_t slot) {
    #if !CFSM_HAS_NVM
      CFSM_UNUSED(slot);
      return false;
    #else
      if (slot >= NVM_SLOTS) return false;
      uint32_t fin = (uint32_t)slotAddress(slot)
                   + sizeof(CfsmStoreHeader) + sizeof(RecipeRecord);
      return fin <= (uint32_t)EEPROM.length();
    #endif
    }
};

#endif /* COREFSM_RECIPE_TYPES_H */
