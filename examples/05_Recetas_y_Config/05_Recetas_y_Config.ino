/* =============================================================================
 *  EJEMPLO 5  -  Recetas, configuracion persistente y aprendizaje (teach-in)
 * -----------------------------------------------------------------------------
 *  Es el ejemplo mas parecido a una maquina de verdad. Muestra las tres capas
 *  de datos que toda maquina industrial separa:
 *
 *    CONFIGURACION  ajustes de la maquina, se tocan al instalarla (EEPROM)
 *    RECETAS        parametros del producto, se cambian a diario (flash+EEPROM)
 *    PROCESO        datos vivos del ciclo actual (RAM, no sobreviven)
 *
 *  Y demuestra el TEACH-IN: llevas los ejes a mano hasta la posicion buena,
 *  pulsas un boton y esa posicion queda grabada en la receta. Es como se
 *  programan de verdad los robots industriales; nadie calcula coordenadas a
 *  mano si puede llevar el brazo alli y decir "aqui".
 *
 *  MONTAJE (Arduino Nano)
 *      A0, A1   potenciometros = realimentacion de posicion de los ejes X e Y
 *               (en una maquina real irian acoplados a los ejes; aqui los
 *               giras tu para simular el movimiento)
 *      D5,D6,D9    driver del eje X (IN1, IN2, PWM)
 *      D7,D8,D10   driver del eje Y (IN1, IN2, PWM)
 *      D2  pulsador MARCHA        -> GND
 *      D3  pulsador CAMBIO RECETA -> GND
 *      D4  pulsador TEACH         -> GND
 *      D11 LED pinza
 *      D12 LED ciclo en marcha
 *      D13 LED alarma
 *
 *  COMANDOS DEL MONITOR SERIE
 *      r  recorre las recetas de fabrica
 *      g  guarda la receta activa en la ranura 0 de EEPROM
 *      l  carga la receta de la ranura 0
 *      c  muestra la configuracion y los contadores
 *      z  reset a valores de fabrica
 *      t  aprende (teach) el paso 0 con las posiciones actuales
 * ========================================================================== */

#include "RecetasPlanta.h"

/* --- Campo ---------------------------------------------------------------- */
DigitalSensor btnMarcha(2, true, 25);
DigitalSensor btnReceta(3, true, 25);
DigitalSensor btnTeach(4,  true, 25);
AnalogSensor  encoderX(A0, 3);       /* realimentacion del eje X */
AnalogSensor  encoderY(A1, 3);
DigitalOutput ledPinza(11);
DigitalOutput ledCiclo(12);
DigitalOutput ledAlarma(13);

MotorDrive motorX(5, 6, 9);
MotorDrive motorY(7, 8, 10);

DeviceManager<8> io;

/* --- Ejes posicionados (lazo cerrado sobre los potenciometros) ------------ */
PositionAxis ejeX(motorX);
PositionAxis ejeY(motorY);

/* --- Datos ---------------------------------------------------------------- */
/* La configuracion vive al principio de la EEPROM; las recetas de usuario
 * empiezan despues. Calcular la direccion con footprint() en vez de a ojo
 * evita el error clasico de que un bloque pise al siguiente cuando anades un
 * campo a la estructura. */
DataBlock<ConfigMaquina, 1, 0> config;
RecipeBank<2, 64>              recetas;   /* 2 ranuras de usuario desde la 64 */

/* --- Ejecutor de recetas -------------------------------------------------- */
class Manipulador : public RecipeExecutor<2> {
  protected:
    /* Aqui es donde la mascara de bits abstracta se convierte en hardware. */
    void applyTool(uint8_t mask) override {
      ledPinza.set(mask & HERR_PINZA);
    }
    /* Confirmacion de agarre. En una maquina real seria un sensor de vacio o
     * un microrruptor en la pinza; aqui se da siempre por bueno. */
    bool toolFeedback(uint8_t mask) override { (void)mask; return true; }
};

Manipulador     manipulador;
BlockManager<2> manager;
StepTracer      tracer(manipulador, Serial);

uint8_t recetaActual = 0;

/* Prototipos. El IDE de Arduino los genera solo, pero declararlos a mano hace
 * que el mismo archivo compile tal cual en PlatformIO o con un Makefile. */
void mostrarReceta();
void consolaSerie();

void mostrarReceta() {
  Serial.print(F("[RECETA] "));
  Serial.print(recetas.active.header.name);
  Serial.print(F("  id="));   Serial.print(recetas.active.header.id);
  Serial.print(F("  pasos=")); Serial.println(recetas.active.header.totalSteps);
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) { }
  Serial.println(F("=== CoreFSM - Recetas y configuracion ==="));

  /* --- Hardware --- */
  io.registerDevice(&btnMarcha, F("MARCHA"));
  io.registerDevice(&btnReceta, F("RECETA"));
  io.registerDevice(&btnTeach,  F("TEACH"));
  io.registerDevice(&encoderX,  F("POS_X"));
  io.registerDevice(&encoderY,  F("POS_Y"));
  io.registerDevice(&ledPinza,  F("PINZA"));
  io.registerDevice(&ledCiclo,  F("CICLO"));
  io.registerDevice(&ledAlarma, F("ALARMA"));
  io.beginAll();
  motorX.begin();
  motorY.begin();

  /* Rampa de arranque: sin ella, el pico de corriente al arrancar hunde la
   * alimentacion y reinicia el microcontrolador. */
  motorX.setRamp(4);
  motorY.setRamp(4);

  /* --- Configuracion persistente --- */
  config.begin();
  Serial.print(F("[CONFIG] "));
  Serial.println(config.resultText());

  /* --- Recetas --- */
  recetas.setFactoryTable(RECETAS_FABRICA, NUM_RECETAS_FABRICA);
  if (!recetas.loadFactory(0)) Serial.println(F("[RECETA] ERROR al cargar"));
  mostrarReceta();

  /* --- Ejes --- */
  /* tune(kp, tolerancia, velocidad_minima, velocidad_maxima).
   * La velocidad minima existe porque por debajo de cierto PWM el motor no
   * llega a girar: sin ella, el eje se quedaria parado a medio camino. */
  ejeX.tune(12, 6, 70, (uint8_t)config.data.velocidadMaxima);
  ejeY.tune(12, 6, 70, (uint8_t)config.data.velocidadMaxima);

  /* Con potenciometro absoluto no hace falta buscar el origen; se declara
   * referenciado directamente. Con encoders incrementales habria que hacer
   * homing contra un final de carrera antes de admitir automatico. */
  ejeX.startHoming(1); ejeX.updateHoming(true);
  ejeY.startHoming(1); ejeY.updateHoming(true);
  ejeX.enable(); ejeY.enable();

  /* --- Ejecutor --- */
  manipulador.attachAxis(0, &ejeX);
  manipulador.attachAxis(1, &ejeY);
  manipulador.setRecipe(&recetas.active);

  manager.registerBlock(&manipulador, F("MANIPULADOR"));
  manager.beginAll();
  manipulador.ST.cfgw.enable = true;

  Serial.println(F("Pulsa MARCHA para ejecutar. 'r'=cambiar receta, 'c'=config."));
}

void loop() {
  /* --- 1. PAE --- */
  io.readAllInputs();

  /* Realimentacion de posicion: el ADC da 0..1023 y los ejes trabajan en esas
   * mismas unidades, asi que no hace falta escalar. */
  ejeX.setFeedback((int16_t)encoderX.value());
  ejeY.setFeedback((int16_t)encoderY.value());

  /* --- 2. Mando --- */
  if (btnMarcha.hasRisen()) {
    if (manipulador.isRunning()) manipulador.stop();
    else                         manipulador.start();
  }

  if (btnReceta.hasRisen() && !manipulador.isRunning()) {
    recetaActual = (recetaActual + 1) % NUM_RECETAS_FABRICA;
    recetas.loadFactory(recetaActual);
    mostrarReceta();
  }

  /* Teach-in: graba la posicion actual de los ejes en el paso 0. */
  if (btnTeach.hasRisen() && !manipulador.isRunning()) {
    manipulador.teachStep(0, HERR_PINZA, 300);
    Serial.print(F("[TEACH] Paso 0 aprendido: X="));
    Serial.print(ejeX.position());
    Serial.print(F(" Y="));
    Serial.println(ejeY.position());
  }

  /* --- 3. Scan --- */
  manager.updateAll();
  ejeX.update();          /* lazo de posicion */
  ejeY.update();

  /* --- 4. Diagnostico --- */
  ledCiclo.setMode(manipulador.isRunning() ? OUT_ON : OUT_OFF);
  ledAlarma.setMode(manipulador.isFaulted() ? OUT_BLINK_FAST : OUT_OFF);
  tracer.update();

  /* Contador de vida de la maquina. Se guarda de forma perezosa: solo si
   * cambio algo y como mucho una vez por minuto, para no desgastar la EEPROM. */
  static uint32_t ultimoCiclo = 0;
  if (manipulador.getCycleCount() != ultimoCiclo) {
    ultimoCiclo = manipulador.getCycleCount();
    config.data.piezasTotales++;
  }
  config.autoSave();

  consolaSerie();

  /* --- 5. PAA --- */
  io.writeAllOutputs();
  motorX.writeOutputs();
  motorY.writeOutputs();
}

void consolaSerie() {
  if (!Serial.available()) return;
  switch (Serial.read()) {

    case 'r':
      if (!manipulador.isRunning()) {
        recetaActual = (recetaActual + 1) % NUM_RECETAS_FABRICA;
        recetas.loadFactory(recetaActual);
        mostrarReceta();
      }
      break;

    case 'g':
      Serial.println(recetas.saveSlot(0)
        ? F("[RECETA] Guardada en la ranura 0")
        : F("[RECETA] No se pudo guardar"));
      break;

    case 'l':
      if (recetas.loadSlot(0)) { Serial.println(F("[RECETA] Cargada de la ranura 0")); mostrarReceta(); }
      else Serial.println(F("[RECETA] Ranura 0 vacia o corrupta"));
      break;

    case 'c':
      Serial.println(F("--- CONFIGURACION ---"));
      Serial.print(F(" velocidad maxima : ")); Serial.println(config.data.velocidadMaxima);
      Serial.print(F(" receta por defecto: ")); Serial.println(config.data.recetaPorDefecto);
      Serial.print(F(" piezas totales    : ")); Serial.println(config.data.piezasTotales);
      Serial.print(F(" cambios sin guardar: ")); Serial.println(config.isDirty() ? F("SI") : F("NO"));
      break;

    case 'z':
      config.factoryReset();
      Serial.println(F("[CONFIG] Borrada. Reinicia para cargar valores de fabrica."));
      break;

    case 't':
      manipulador.teachStep(0, HERR_PINZA, 300);
      Serial.println(F("[TEACH] Paso 0 aprendido"));
      break;
  }
}
