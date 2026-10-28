# Registro de pasos (setup del entorno)

Resumen de lo que hemos hecho y por qué, para poder explicarlo en la evaluación.
Equipo de trabajo: PC con CachyOS (Arch Linux).

## 28/10/2026

### 1. Repositorio GitHub
- Repo: https://github.com/Lluis05/SWG (rama `main`).
- Estructura: `firmware/` (placa), `app/` (Flutter), `cloud/` (Report 2), `docs/`.
- `docs/protocol.md`: contrato BLE congelado el 23/10 (UUIDs, trama de 15 bytes,
  interfaz Dart). Cualquier cambio de la trama se apunta ahí primero.
- Cada miembro trabaja en su rama y se integra con pull requests.

### 2. Comprobar que el PC ve la placa
- `lsusb` muestra el depurador de la DK: `SEGGER J-Link`, nº de serie `000683296431`.
- La nRF52840-DK lleva un J-Link integrado (Interface MCU) que sirve para
  programar el nRF52840 y, además, ofrece un puerto serie virtual (VCOM) para logs.
- Primera conexión: el J-Link aparecía como `1366:0101` con una sola interfaz USB
  (sin puerto serie ni unidad de almacenamiento). Tras instalar el software J-Link y
  reconectar la placa aparece como `1366:1025` con 4 interfaces (CDC = puerto serie,
  Mass Storage, y la interfaz de depuración). Causa exacta: TODO (no verificada).

### 3. Software SEGGER J-Link
- Comando: `paru -S jlink-software-and-documentation` (paquete AUR).
- Para qué: es el driver/herramienta que habla con el J-Link de la placa para
  flashear y depurar. También instala las **reglas udev** (`99-jlink.rules`)
  para poder usar la placa sin ser root.
- Licencia SEGGER: uso permitido con productos SEGGER/OEM autorizados (la DK de Nordic lo es).

### 4. nrfutil (herramienta de Nordic)
- Descargado de la web oficial de Nordic a `~/.local/bin/nrfutil` (versión 8.2.1).
- Módulos instalados: `nrfutil install device sdk-manager`
  - `device`: listar, flashear, borrar y resetear placas.
  - `sdk-manager`: instalar el nRF Connect SDK y su toolchain.
- Comprobación: `nrfutil device list` →
  `683296431, J-Link, PCA10056, /dev/ttyACM0` (PCA10056 = nRF52840-DK). ✔

### 5. nRF Connect SDK
- Versión elegida: **v3.4.1** (última estable; la v3.4.2 aún es release candidate).
- Comando: `nrfutil sdk-manager install v3.4.1`
- Instala el SDK (Zephyr + librerías de Nordic, gestionado con `west`) y la
  toolchain (compilador ARM GCC, CMake, Ninja, Python…) en `~/ncs`.
- Por qué nRF Connect SDK/Zephyr y no Arduino: la guía del curso indica que la
  librería BLE de Arduino falla desde 2024; Zephyr es el framework oficial de Nordic.
- Estado: instalado en `~/ncs/v3.4.1` (Zephyr OS v4.4.2). ✔

### 6. Primer firmware: ejemplo `peripheral_lbs`
- Objetivo: comprobar que compilar, flashear y BLE funcionan **antes** de escribir
  nuestro código. No hace falta ningún sensor conectado.
- Qué hace el ejemplo (LBS = LED Button Service de Nordic):
  - LED1 parpadea = el programa está corriendo. LED2 encendido = móvil conectado.
  - El móvil puede encender/apagar el LED3 (escritura GATT).
  - Al pulsar el Botón 1 la placa notifica al móvil (notificación GATT).
  - Tiene seguridad activada: al emparejar, la placa muestra un código de 6 dígitos
    por el puerto serie y hay que introducirlo en el móvil.
- Copiado de `~/ncs/v3.4.1/nrf/samples/bluetooth/peripheral_lbs` a `firmware/`.
  Borrados los archivos de otras placas (nRF53/nRF54, Thingy:53) que no usamos.
- Compilar (desde `firmware/`):
  ```
  export ZEPHYR_BASE=$HOME/ncs/v3.4.1/zephyr
  nrfutil sdk-manager toolchain launch --ncs-version v3.4.1 -- west build -b nrf52840dk/nrf52840 -d build .
  ```
  Resultado: FLASH 193856 B / 1 MB (18.5 %), RAM 30360 B / 256 KB (11.6 %).
- Flashear:
  ```
  nrfutil sdk-manager toolchain launch --ncs-version v3.4.1 -- west flash -d build --erase
  ```
  `--erase` borra toda la flash antes (ERASEALL). Resultado: programado y verificado. ✔
- Ver logs: `cat /dev/ttyACM0` (puerto serie del J-Link, 115200 baudios).
  Log de arranque: `Booting nRF Connect SDK v3.4.1`, `Bluetooth initialized`,
  `Advertising successfully started`. ✔
- Prueba con el móvil (app nRF Connect): conexión, emparejamiento con passkey,
  notificaciones del Botón 1 (`00`/`01`) y escritura del LED3 funcionan. ✔
- Nota: el código es el ejemplo de Nordic **sin modificar** (no lo hemos programado
  nosotros). Solo lo hemos copiado, compilado y flasheado. Nuestro firmware se
  escribirá a partir de él.
