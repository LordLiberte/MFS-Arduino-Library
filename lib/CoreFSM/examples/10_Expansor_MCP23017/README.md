# 10 · Expansor MCP23017

Conecta un MCP23017 a `Wire` con dirección `0x20`. GPA0 es una entrada con
pull-up y GPB0 una salida. El ejemplo demuestra las tablas de backend que el
generador también produce desde CSV o JSON.

El backend mantiene una imagen de 16 bits y agrupa las operaciones I2C por
scan. No ofrece PWM, lectura rápida de encoders ni funciones de seguridad.

