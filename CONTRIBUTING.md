# Contribuir a MFS Arduino Library

Gracias por mejorar CoreFSM. El objetivo es conservar un runtime pequeño,
determinista y comprensible en microcontroladores con pocos recursos.

## Antes de cambiar código

1. Abre una incidencia con el caso reproducible si se trata de un defecto.
2. Separa cambios de biblioteca, documentación y proyectos no relacionados.
3. No añadas memoria dinámica, esperas bloqueantes ni I/O serie ilimitada al
   camino normal del scan.
4. Una función que afecte actuadores debe documentar su estado de arranque,
   fallo, timeout y recuperación.

## Comprobar el cambio

Desde la raíz del repositorio:

```bash
bash lib/CoreFSM/tests/ejecutar.sh
python -m unittest discover -s lib/CoreFSM/tools/tests -v
python lib/CoreFSM/tools/corefsm_gen.py --project projects/00_TestLibrary --check
```

Compila además el proyecto afectado y los ejemplos con PlatformIO. El mismo
conjunto se ejecuta en GitHub Actions.

## Pull request

- Explica el comportamiento anterior y el nuevo.
- Añade una prueba que falle antes de la corrección cuando sea posible.
- Actualiza README, referencia de API y `CHANGELOG.md` si cambia el contrato.
- No subas `.pio`, cachés Python, credenciales, licencias locales de Wokwi ni
  archivos del IDE con rutas absolutas.
- Mantén `HardwareConfig.h` versionado y sincronizado con su fuente.

La API pública debe conservar compatibilidad siempre que no impida corregir un
fallo grave. Si el cambio es incompatible, descríbelo expresamente y justifica
la migración.
