#ifndef COREFSM_RECIPE_EXECUTOR_H
#define COREFSM_RECIPE_EXECUTOR_H

#include "../core/SequenceBlock.h"
#include "../core/IAxis.h"
#include "RecipeTypes.h"

/* ===========================================================================
 *  RecipeExecutor.h  -  Interprete generico de recetas
 * ---------------------------------------------------------------------------
 *  Es la pieza que convierte una tabla de datos en movimiento. Su codigo NO
 *  contiene ni una sola coordenada ni un solo tiempo: todo sale de la receta
 *  activa. Por eso el mismo ejecutor sirve para un brazo de paletizado, un
 *  dosificador o una mesa de dos ejes.
 *
 *  MAQUINA DE ESTADOS INTERNA
 *  --------------------------
 *  Cada paso de la receta se ejecuta atravesando siempre la misma secuencia
 *  fija de cinco fases:
 *
 *      CARGAR      lee el paso de la receta y programa las consignas
 *         |
 *      MOVER       espera a que todos los ejes activos lleguen a destino
 *         |
 *      ASENTAR     espera el tiempo de permanencia (dwell)
 *         |
 *      HERRAMIENTA acciona pinza / ventosa / lo que sea, y espera su tiempo
 *         |
 *      SIGUIENTE   avanza el indice; si era el ultimo, cierra el ciclo
 *
 *  Separar el movimiento de la accion de la herramienta no es un capricho: si
 *  la pinza se cerrara mientras el brazo aun se mueve, agarraria el aire. Y
 *  el tiempo de asentamiento existe porque un eje que acaba de frenar sigue
 *  oscilando unas decimas; cerrar la pinza en ese instante da un agarre malo.
 *
 *  CONEXION CON EL HARDWARE
 *  ------------------------
 *  El ejecutor no sabe que hay detras de cada eje: habla con punteros a IAxis.
 *  Y no sabe que hace cada bit de la mascara de herramienta: te llama a ti
 *  mediante el hook applyTool(). Asi puedes gobernar lo que quieras sin tocar
 *  el ejecutor.
 * ======================================================================== */

enum RecipeStepPhase : uint16_t {
  RX_IDLE        = 0,
  RX_LOAD        = 10,
  RX_MOVE        = 20,
  RX_SETTLE      = 30,
  RX_TOOL        = 40,
  RX_NEXT        = 50,
  RX_FINISHED    = 60
};

template <uint8_t NUM_AXES = CFSM_RECIPE_AXES>
class RecipeExecutor : public SequenceBlock {
  public:
    static_assert(NUM_AXES <= CFSM_RECIPE_AXES,
                  "NUM_AXES no puede superar CFSM_RECIPE_AXES");

    RecipeExecutor() : _recipe(nullptr), _index(0), _toolMask(0) {
      for (uint8_t i = 0; i < NUM_AXES; i++) _axes[i] = nullptr;
    }

    /* Conecta un eje fisico a la posicion i de las recetas. El indice debe
     * coincidir con el orden de AxisMotion axis[] dentro de RecipeStep. */
    void attachAxis(uint8_t i, IAxis* axis) {
      if (i < NUM_AXES) _axes[i] = axis;
    }

    /* Apunta a la receta que se va a ejecutar. Normalmente &bank.active. */
    void setRecipe(RecipeRecord* r) {
      _recipe = r;
      setCycleTimeout(r ? (cfsm_time_t)r->header.maxCycleMs : 0);
    }

    RecipeRecord* recipe() const { return _recipe; }
    uint8_t       stepIndex() const { return _index; }
    uint8_t       toolMask() const { return _toolMask; }

    /* Sensor externo que consultan los pasos con TRIG_SENSOR. Actualizalo
     * desde el .ino antes del scan. */
    void setSensorState(uint8_t id, bool value) {
      if (id < 8) { if (value) _sensors |= (1 << id); else _sensors &= ~(1 << id); }
    }

    void begin() override {
      setInitialStep(RX_IDLE);
      setStep(RX_IDLE);
      _index    = 0;
      _toolMask = 0;
    }

    void update() override {
      if (!updateSequence()) { onInactive(); return; }

      /* Sin receta valida no se puede hacer nada; mejor decirlo que quedarse
       * dando vueltas en silencio.
       *
       * El limite superior no es paranoia: setRecipe() acepta cualquier
       * puntero, y una RecipeRecord declarada en el sketch sin pasar por
       * cfsmClearRecipe() nace con basura. Un totalSteps de 200 sobre un array
       * de 8 no da excepcion en un microcontrolador: da lecturas de RAM
       * arbitraria y comportamiento aleatorio imposible de depurar. */
      if (!_recipe || _recipe->header.totalSteps == 0 ||
          _recipe->header.totalSteps > CFSM_RECIPE_MAX_STEPS) {
        fault(CFSM_ERR_RECIPE_INVALID);
        return;
      }

      switch (_currentStep) {

        /* ----------------------------------------------------------------- */
        case RX_IDLE:
          holdAllAxes();
          _index    = 0;
          _toolMask = 0;
          applyTool(0);
          setStep(RX_LOAD);
          break;

        /* ----------------------------------------------------------------- */
        case RX_LOAD: {
          const RecipeStep& s = _recipe->steps[_index];

          /* Un eje sin referenciar no sabe donde esta: sus coordenadas no
           * significan nada y moverlo seria peligroso. */
          for (uint8_t i = 0; i < NUM_AXES; i++) {
            if (s.axis[i].enabled && !_axes[i]) {
              fault(CFSM_ERR_RECIPE_INVALID);
              return;
            }
            if (s.axis[i].enabled && !_axes[i]->isHomed()) {
              fault(CFSM_ERR_NOT_HOMED);
              return;
            }
          }

          for (uint8_t i = 0; i < NUM_AXES; i++) {
            if (!_axes[i]) continue;
            if (s.axis[i].enabled) _axes[i]->moveTo(s.axis[i].target, s.axis[i].speed);
            else                   _axes[i]->hold();
          }

          cfsm_time_t to = s.transition.timeoutMs;
          setStep(RX_MOVE, to);
          break;
        }

        /* ----------------------------------------------------------------- */
        case RX_MOVE: {
          const RecipeStep& s = _recipe->steps[_index];
          bool ready = false;

          switch (s.transition.trigger) {
            case TRIG_TIMER:
              ready = (getTimeInStep() >= s.transition.dwellMs);
              break;
            case TRIG_SENSOR:
              ready = sensor(s.transition.sensorId);
              break;
            case TRIG_MANUAL:
              ready = _manualAdvance;
              break;
            case TRIG_POSITION_TIME:
              ready = axesInPosition(s) && (getTimeInStep() >= s.transition.dwellMs);
              break;
            default:
              ready = axesInPosition(s);
              break;
          }

          if (ready) {
            _manualAdvance = false;
            /* Con TRIG_TIMER y TRIG_POSITION_TIME, la espera de dwellMs YA se
             * ha consumido aqui. Volver a esperarla en RX_SETTLE duplicaria el
             * tiempo real del paso respecto a lo que dice la receta, y si el
             * timeout fuera menor que el doble del dwell el paso caeria en
             * alarma sin motivo. */
            _dwellDone = (s.transition.trigger == TRIG_TIMER ||
                          s.transition.trigger == TRIG_POSITION_TIME);
            setStep(RX_SETTLE);
          }
          break;
        }

        /* ----------------------------------------------------------------- */
        case RX_SETTLE: {
          const RecipeStep& s = _recipe->steps[_index];
          holdAllAxes();
          cfsm_time_t espera = _dwellDone ? 0 : s.transition.dwellMs;
          if (getTimeInStep() >= espera) setStep(RX_TOOL);
          break;
        }

        /* ----------------------------------------------------------------- */
        case RX_TOOL: {
          const RecipeStep& s = _recipe->steps[_index];
          if (isFirstScanInStep()) {
            _toolMask = s.tool.outputs;
            applyTool(_toolMask);
          }
          if (getTimeInStep() >= s.tool.settleMs) {
            /* Si el paso exige confirmacion (por ejemplo, sensor de pieza
             * agarrada) y no llega, es un fallo real: seguir sin pieza
             * estropearia el resto del ciclo. */
            if (s.tool.requireFeedback && !toolFeedback(_toolMask)) {
              fault(CFSM_ERR_SENSOR_INVALID);
              return;
            }
            setStep(RX_NEXT);
          }
          break;
        }

        /* ----------------------------------------------------------------- */
        case RX_NEXT:
          _index++;
          if (_index >= _recipe->header.totalSteps) setStep(RX_FINISHED);
          else                                      setStep(RX_LOAD);
          break;

        /* ----------------------------------------------------------------- */
        case RX_FINISHED:
          holdAllAxes();
          _index = 0;
          completeCycle(RX_IDLE); /* cuenta la pieza y decide si encadena otra */
          break;
      }
    }

    /* Autoriza el salto en los pasos con TRIG_MANUAL. */
    void advanceManual() { _manualAdvance = true; }

    const __FlashStringHelper* stepName(uint16_t s) const override {
      switch (s) {
        case RX_IDLE:     return CFSM_FSTR("REPOSO");
        case RX_LOAD:     return CFSM_FSTR("CARGAR_PUNTO");
        case RX_MOVE:     return CFSM_FSTR("MOVER_EJES");
        case RX_SETTLE:   return CFSM_FSTR("ASENTAR");
        case RX_TOOL:     return CFSM_FSTR("HERRAMIENTA");
        case RX_NEXT:     return CFSM_FSTR("SIGUIENTE");
        case RX_FINISHED: return CFSM_FSTR("FIN_RECETA");
        default:          return nullptr;
      }
    }

    /* -----------------------------------------------------------------------
     *  APRENDIZAJE (TEACH-IN)
     *  Mueves el brazo a mano hasta la posicion buena, pulsas un boton y la
     *  posicion queda grabada en el paso indicado de la receta. Es como se
     *  programan de verdad los robots industriales: nadie calcula coordenadas
     *  a mano si puede llevar el brazo alli y decir "aqui".
     * -------------------------------------------------------------------- */
    bool teachStep(uint8_t stepIdx, uint8_t toolMask = 0, uint16_t dwellMs = 300,
                   uint8_t velocidad = 150, int16_t tolerancia = 8,
                   uint16_t timeoutMs = 8000) {
      if (!_recipe || stepIdx >= CFSM_RECIPE_MAX_STEPS) return false;
      RecipeStep& s = _recipe->steps[stepIdx];
      for (uint8_t i = 0; i < NUM_AXES; i++) {
        if (!_axes[i]) { s.axis[i].enabled = false; continue; }
        s.axis[i].target    = _axes[i]->position();
        s.axis[i].enabled   = true;
        /* Estos tres campos hay que rellenarlos SIEMPRE, no solo el destino.
         * Sobre una receta recien puesta a cero valdrian 0, y una tolerancia
         * de 0 es inalcanzable para un eje real: el ejecutor se quedaria
         * colgado en RX_MOVE esperando una llegada exacta que nunca ocurre.
         * Y con timeout 0 la vigilancia esta desactivada, asi que se colgaria
         * en silencio, sin alarma. */
        s.axis[i].speed     = velocidad;
        s.axis[i].tolerance = tolerancia;
      }
      s.tool.outputs         = toolMask;
      s.tool.settleMs        = dwellMs;
      s.tool.requireFeedback = false;
      s.transition.trigger   = TRIG_POSITION;
      s.transition.dwellMs   = dwellMs;
      s.transition.timeoutMs = timeoutMs;
      s.transition.sensorId  = 0;
      if (stepIdx >= _recipe->header.totalSteps)
        _recipe->header.totalSteps = stepIdx + 1;
      return true;
    }

  protected:
    /* HOOK OBLIGATORIO: traduce la mascara de bits a tus actuadores reales.
     *
     *      void applyTool(uint8_t mask) override {
     *        HW.Pinza.set(mask & 0x01);
     *        HW.Vacio.set(mask & 0x02);
     *      }
     */
    virtual void applyTool(uint8_t mask) { CFSM_UNUSED(mask); }

    /* HOOK OPCIONAL: confirmacion de que la herramienta hizo su trabajo. */
    virtual bool toolFeedback(uint8_t mask) { CFSM_UNUSED(mask); return true; }

    /* HOOK OPCIONAL: que hacer cuando el bloque no esta en marcha. Por defecto
     * se detienen todos los ejes, que es lo seguro. */
    virtual void onInactive() { holdAllAxes(); }

    void holdAllAxes() {
      for (uint8_t i = 0; i < NUM_AXES; i++) if (_axes[i]) _axes[i]->hold();
    }

    IAxis* axisAt(uint8_t i) const { return (i < NUM_AXES) ? _axes[i] : nullptr; }

  private:
    IAxis*        _axes[NUM_AXES];
    RecipeRecord* _recipe;
    uint8_t       _index;
    uint8_t       _toolMask;
    uint8_t       _sensors = 0;
    bool          _manualAdvance = false;
    bool          _dwellDone     = false;  /* la espera ya se consumio en RX_MOVE */

    bool sensor(uint8_t id) const { return (id < 8) && (_sensors & (1 << id)); }

    bool axesInPosition(const RecipeStep& s) const {
      for (uint8_t i = 0; i < NUM_AXES; i++) {
        if (!s.axis[i].enabled) continue;
        if (!_axes[i]) return false;
        if (!_axes[i]->inPosition(s.axis[i].tolerance)) return false;
      }
      return true;
    }
};

#endif /* COREFSM_RECIPE_EXECUTOR_H */
