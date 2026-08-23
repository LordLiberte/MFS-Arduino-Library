#ifndef COREFSM_CONFIG_STORE_H
#define COREFSM_CONFIG_STORE_H

#include "../core/CoreFSM_Platform.h"

#if CFSM_HAS_NVM
  #include <EEPROM.h>
#endif

/* ===========================================================================
 *  ConfigStore.h  -  Configuracion persistente con validacion
 * ---------------------------------------------------------------------------
 *  QUE ES LA "CONFIGURACION" Y QUE NO LO ES
 *  ----------------------------------------
 *  Conviene separar tres cosas que a menudo se mezclan:
 *
 *    CONFIGURACION  Parametros de la maquina que se tocan en la puesta en
 *                   marcha y casi nunca despues: tiempos de proceso,
 *                   velocidades, calibraciones, limites de seguridad, numero
 *                   de serie. Cambian poco, deben sobrevivir al apagado.
 *                   -> Va aqui, en ConfigStore.
 *
 *    RECETAS        Juegos de parametros que se cambian a diario segun el
 *                   producto que se este fabricando.
 *                   -> Va en RecipeBank.
 *
 *    DATOS DE PROCESO  Valores vivos del ciclo actual: paso, contadores,
 *                   posiciones. Cambian miles de veces por segundo y solo
 *                   algunos merecen sobrevivir al apagado.
 *                   -> Va en DataBlock.
 *
 *  POR QUE HACE FALTA VALIDAR LO QUE SE LEE
 *  ----------------------------------------
 *  Una memoria no volatil devuelve SIEMPRE algo, incluso si nunca se escribio.
 *  Una EEPROM virgen devuelve 0xFF en todos sus bytes. Si cargas eso
 *  directamente en tu estructura de configuracion, acabas con tiempos de
 *  65535 ms y velocidades de 255, y la maquina se comporta de forma absurda
 *  sin dar ningun error.
 *
 *  Peor todavia: si cambias la estructura de configuracion (anades un campo)
 *  y recompilas, los bytes viejos se reinterpretan con la disposicion nueva.
 *  Los valores se mezclan entre campos y sale una configuracion sin sentido
 *  que ademas parece valida.
 *
 *  Por eso cada bloque guardado lleva una cabecera con cuatro comprobaciones:
 *
 *    magic    Marca fija. Si no aparece, ahi nunca se escribio nada nuestro.
 *    version  Numero que TU subes al cambiar la estructura. Si no coincide,
 *             los datos son de una version anterior y no se pueden usar.
 *    size     Tamano de la estructura. Segunda red por si olvidas subir la
 *             version tras anadir un campo.
 *    crc16    Suma de comprobacion. Detecta corrupcion real: un corte de
 *             tension a mitad de escritura, o una celda de flash agotada.
 *
 *  Si algo no cuadra, se cargan los valores por defecto del programa y se
 *  avisa. Es infinitamente mejor arrancar con los valores de fabrica y decirlo
 *  que arrancar con basura en silencio.
 *
 *  SOBRE EL DESGASTE DE LA MEMORIA
 *  -------------------------------
 *  Una celda de EEPROM aguanta unos 100.000 ciclos de escritura. Parece mucho,
 *  pero si guardas la configuracion en cada vuelta del loop() a 1000 scans por
 *  segundo, la destruyes en menos de dos minutos.
 *
 *  REGLA: guarda solo cuando algo haya cambiado de verdad, y solo tras una
 *  accion deliberada del operario. save() de esta clase ya compara antes de
 *  escribir y no toca la memoria si el contenido es identico, pero eso no te
 *  exime de no llamarlo dentro del ciclo de scan.
 * ======================================================================== */

/* CRC-16/CCITT. Implementado bit a bit: es unas cuantas veces mas lento que
 * con tabla, pero una tabla ocuparia 512 bytes de flash y esto solo se ejecuta
 * al guardar y al cargar, no en el ciclo de scan. */
inline uint16_t cfsmCrc16(const uint8_t* data, uint16_t len) {
  uint16_t crc = 0xFFFF;
  while (len--) {
    crc ^= (uint16_t)(*data++) << 8;
    for (uint8_t i = 0; i < 8; i++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

struct CfsmStoreHeader {
  uint16_t magic;
  uint16_t version;
  uint16_t size;
  uint16_t crc;
};

#define CFSM_STORE_MAGIC 0xC0FB   /* "CoreFSM Block" */

/* Tamano de memoria a reservar en las plataformas con EEPROM emulada. */
#ifndef CFSM_NVM_SIZE_DEFAULT
  #define CFSM_NVM_SIZE_DEFAULT 512
#endif

enum CfsmStoreResult : uint8_t {
  CFSM_STORE_OK          = 0,
  CFSM_STORE_EMPTY       = 1,  /* nunca se escribio nada ahi        */
  CFSM_STORE_BAD_VERSION = 2,  /* datos de una version anterior     */
  CFSM_STORE_BAD_SIZE    = 3,  /* la estructura cambio de tamano    */
  CFSM_STORE_BAD_CRC     = 4,  /* datos corruptos                   */
  CFSM_STORE_NO_NVM      = 5   /* esta placa no tiene memoria no volatil */
};

/* ---------------------------------------------------------------------------
 *  ConfigStore<T>
 *    T       : tu estructura de configuracion. Debe ser POD (solo tipos
 *              simples y otras estructuras POD): nada de punteros, String,
 *              ni objetos con constructor. Se guarda copiando bytes en crudo,
 *              y un puntero guardado no significa nada tras un reinicio.
 *    VERSION : subelo cada vez que cambies T.
 *    ADDRESS : direccion de inicio en la memoria no volatil.
 * ------------------------------------------------------------------------ */
template <typename T, uint16_t VERSION = 1, uint16_t ADDRESS = 0>
class ConfigStore {
  public:
    /* Los valores de fabrica. Se usan cuando lo guardado no es valido. */
    T data;

    ConfigStore() : _lastResult(CFSM_STORE_EMPTY) {}

    /* Prepara la memoria. En ESP32/RP2040 la EEPROM es una emulacion sobre
     * flash y hay que reservar el sector antes de usarla. */
    void begin(uint16_t nvmSize = CFSM_NVM_SIZE_DEFAULT) {
      CFSM_UNUSED(nvmSize);
    #if CFSM_HAS_NVM && CFSM_NVM_NEEDS_COMMIT
      EEPROM.begin(nvmSize);
    #endif
    }

    /* Carga y valida. Si algo falla, deja 'data' con los valores por defecto
     * que el programa tuviera y devuelve el motivo. */
    CfsmStoreResult load() {
    #if !CFSM_HAS_NVM
      _lastResult = CFSM_STORE_NO_NVM;
      return _lastResult;
    #else
      CfsmStoreHeader h;
      readBytes(ADDRESS, (uint8_t*)&h, sizeof(h));

      if (h.magic != CFSM_STORE_MAGIC)  { _lastResult = CFSM_STORE_EMPTY;       return _lastResult; }
      if (h.version != VERSION)         { _lastResult = CFSM_STORE_BAD_VERSION; return _lastResult; }
      if (h.size != (uint16_t)sizeof(T)){ _lastResult = CFSM_STORE_BAD_SIZE;    return _lastResult; }

      T tmp;
      readBytes(ADDRESS + sizeof(h), (uint8_t*)&tmp, sizeof(T));

      if (cfsmCrc16((const uint8_t*)&tmp, sizeof(T)) != h.crc) {
        _lastResult = CFSM_STORE_BAD_CRC;
        return _lastResult;
      }

      data = tmp;                       /* solo ahora se acepta */
      _lastResult = CFSM_STORE_OK;
      return _lastResult;
    #endif
    }

    /* Guarda. Devuelve false si no hay memoria disponible.
     * No escribe si el contenido ya es identico byte a byte: proteccion basica
     * contra el desgaste. */
    bool save() {
    #if !CFSM_HAS_NVM
      return false;
    #else
      CfsmStoreHeader h;
      h.magic   = CFSM_STORE_MAGIC;
      h.version = VERSION;
      h.size    = (uint16_t)sizeof(T);
      h.crc     = cfsmCrc16((const uint8_t*)&data, sizeof(T));

      bool changed = writeBytes(ADDRESS, (const uint8_t*)&h, sizeof(h));
      changed |= writeBytes(ADDRESS + sizeof(h), (const uint8_t*)&data, sizeof(T));

    #if CFSM_NVM_NEEDS_COMMIT
      if (changed) EEPROM.commit();
    #endif
      _lastResult = CFSM_STORE_OK;
      return true;
    #endif
    }

    /* Borra la marca para que el proximo arranque cargue los valores de
     * fabrica. Es el "reset a valores de fabrica" de un equipo comercial. */
    bool erase() {
    #if !CFSM_HAS_NVM
      return false;
    #else
      uint16_t zero = 0;
      writeBytes(ADDRESS, (const uint8_t*)&zero, sizeof(zero));
      #if CFSM_NVM_NEEDS_COMMIT
        EEPROM.commit();
      #endif
      return true;
    #endif
    }

    CfsmStoreResult lastResult() const { return _lastResult; }
    bool isValid() const { return _lastResult == CFSM_STORE_OK; }

    /* Cuantos bytes ocupa este bloque. Utilzalo para calcular la direccion del
     * siguiente bloque y que no se solapen. */
    static uint16_t footprint() { return sizeof(CfsmStoreHeader) + sizeof(T); }
    static uint16_t nextAddress() { return ADDRESS + footprint(); }

    const __FlashStringHelper* resultText() const {
      switch (_lastResult) {
        case CFSM_STORE_OK:          return CFSM_FSTR("Configuracion cargada");
        case CFSM_STORE_EMPTY:       return CFSM_FSTR("Memoria vacia: valores de fabrica");
        case CFSM_STORE_BAD_VERSION: return CFSM_FSTR("Version antigua: valores de fabrica");
        case CFSM_STORE_BAD_SIZE:    return CFSM_FSTR("Tamano distinto: valores de fabrica");
        case CFSM_STORE_BAD_CRC:     return CFSM_FSTR("CRC erroneo: datos corruptos");
        default:                     return CFSM_FSTR("Sin memoria no volatil");
      }
    }

  private:
    CfsmStoreResult _lastResult;

  #if CFSM_HAS_NVM
    static void readBytes(uint16_t addr, uint8_t* dst, uint16_t len) {
      for (uint16_t i = 0; i < len; i++) dst[i] = EEPROM.read(addr + i);
    }
    /* Escribe solo los bytes que cambian. Devuelve true si toco algo. */
    static bool writeBytes(uint16_t addr, const uint8_t* src, uint16_t len) {
      bool changed = false;
      for (uint16_t i = 0; i < len; i++) {
        if (EEPROM.read(addr + i) != src[i]) {
          EEPROM.write(addr + i, src[i]);
          changed = true;
        }
      }
      return changed;
    }
  #endif
};

#endif /* COREFSM_CONFIG_STORE_H */
