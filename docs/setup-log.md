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

## 04/11/2026

### 7. Integrar la app de Pol en `main`
- Pol subió la rama `pol/edge-app` (app Flutter completa + `docs/edge-app.md`).
- Revisada: el decodificador lee la trama exactamente como dice `protocol.md`.
- Merge a `main` como *fast-forward* (`main` no tenía commits nuevos, así que solo
  se "adelanta" el puntero de `main`; no se crea commit de merge).
- Aceptado el cambio de Pol en la interfaz Dart: `ReadingSource` tiene `start()` y
  `stop()` para poder cambiar entre datos simulados y la placa en marcha.

### 8. Firmware paso 2: nuestro servicio BLE (rama `lluis/firmware-step2`)
Objetivo: que la placa deje de ser el ejemplo de Nordic y envíe **nuestra trama de
15 bytes cada 2 s**, con valores fijos de prueba (aún sin botones ni sensor).

Cambios:
- **`Kconfig` borrado**: solo definía la opción de seguridad del ejemplo.
- **`prj.conf`**: nombre `SowGuard`, fuera el servicio LBS. No activamos
  `CONFIG_BT_SMP` (la parte de BLE que hace el emparejamiento) → **no hay passkey**.
- **`src/main.c`** reescrito en 5 secciones:
  1. **UUIDs + servicio GATT** (`BT_GATT_SERVICE_DEFINE`). Es una tabla de
     "atributos" que el stack BLE registra solo al arrancar:
     `attrs[0]` servicio, `attrs[1]` declaración de la característica (Notify),
     `attrs[2]` valor (la trama), `attrs[3]` CCCD.
     El **CCCD** es el "interruptor" de 2 bytes que el móvil pone a `0x0001` para
     pedir notificaciones; cuando lo hace, se ejecuta `ccc_changed()` y ponemos
     `notify_enabled = true`.
  2. **Advertising**: el paquete de anuncio (máx. 31 B) lleva flags (3 B) + nuestro
     UUID de servicio (18 B). Es obligatorio: la app escanea filtrando por ese UUID.
     El nombre va en la *scan response* (paquete extra que el móvil pide).
  3. **Conexión**: al conectar se enciende LED2; al desconectar se apaga y
     `notify_enabled = false`. `recycled_cb` vuelve a anunciar para poder reconectar.
  4. **Trama**: `build_frame()` rellena los 15 bytes con `sys_put_le16/32`, que
     escriben el número **byte menos significativo primero** (little-endian).
     `print_frame()` la imprime en hexadecimal por el puerto serie (evidencia).
  5. **`main()`**: LEDs + Bluetooth + advertising y un bucle cada 2 s: LED1
     parpadea y, **solo si el móvil está suscrito**, se envía la trama con
     `bt_gatt_notify()`. Si el envío va bien, `seq++`.
- **Decisión 04/11**: `seq` solo aumenta cuando la trama se envía de verdad. Así un
  hueco en `seq` = trama perdida en el enlace BLE (no tiempo desconectado).
- Valores fijos: 23,45 °C, 55 %, 350 mL/min, de pie, sin flags (los mismos que el
  primer test del decodificador de la app).
- Compilado con `west build -p always` (`-p` = *pristine*, desde cero, necesario
  porque borramos `Kconfig`): FLASH 136168 B (13 %), RAM 23540 B (9 %).
  Antes con LBS + seguridad: 193856 B / 30360 B.

### 9. Instalar Flutter en este PC (para compilar la app nosotros)
Todo en la carpeta personal (sin sudo), excepto Java:
- **Flutter 3.47.9** (misma versión que Pol, Dart 3.13.7):
  `git clone --depth 1 -b 3.47.9 https://github.com/flutter/flutter.git ~/development/flutter`
- **Java 17**: `sudo pacman -S --needed jdk17-openjdk` (el proyecto Android pide Java 17).
- **SDK de Android** (sin Android Studio): *command-line tools* de Google en
  `~/Android/Sdk/cmdline-tools/latest`, licencias con `sdkmanager --licenses`, y
  `sdkmanager "platform-tools" "platforms;android-36" "build-tools;36.0.0"`.
  Gradle descargó solo el NDK la primera vez.
- Configuración: `flutter config --android-sdk ~/Android/Sdk` y
  `flutter config --jdk-dir /usr/lib/jvm/java-17-openjdk`.
- Licencias aceptadas: 8. La única que usamos es `android-sdk-license` (gratis,
  solo para desarrollar apps Android, no se puede redistribuir el SDK). Las demás
  son de componentes que no instalamos (Google TV, XR, Glass, MIPS, emulador ARM).
- `flutter test` en `app/` → **13 tests pasan**.

### 10. Instalar la app en el Xiaomi (sin SIM)
- Xiaomi bloquea `adb install` (`INSTALL_FAILED_USER_RESTRICTED`) si no activas
  "Instalar vía USB", y esa opción pide SIM.
- Solución: compilar el APK y copiarlo al móvil para instalarlo a mano:
  ```
  flutter build apk --debug                      # desde app/
  adb push build/app/outputs/flutter-apk/app-debug.apk /sdcard/Download/sowguard-debug.apk
  ```
  En el móvil: Archivos → Descargas → tocar el APK → permitir "apps desconocidas".
- Ver los logs de la app desde el PC: `adb logcat -s flutter | grep "BLE:"`

### 11. Primera prueba de extremo a extremo (placa → BLE → app) ✔
- App en "Real board": encuentra el nodo por UUID, conecta sin código y activa
  notificaciones (log de la placa: `Connected`, `Notifications enabled`).
- 32 tramas recibidas, **0 perdidas (0,0 %)**, una cada ~2 s.
- Comprobación byte a byte, trama `01 00 00 32 f8 10 00 29 09 7c 15 5e 01 00 00`:
  `01` versión · `00 00` seq 0 · `32 f8 10 00` → `0x0010F832` = 1112114 ms ·
  `29 09` → 2345 = 23,45 °C · `7c 15` → 5500 = 55 % · `5e 01` → 350 mL/min ·
  `00` de pie · `00` sin flags. La app muestra lo mismo.
- Detalle: la app enseña 23.4 °C y no 23.5: 23,45 en coma flotante es 23,4499…
  y `toStringAsFixed(1)` redondea hacia abajo. No es un error de la trama.
- Evidencias en `docs/evidence/`: `2026-11-04_step2_board_uart.log`,
  `2026-11-04_step2_app_logcat.log`, `2026-11-04_step2_app_connected.png`.

### 12. Firmware paso 3: simulación con los botones
Objetivo: que la placa simule lo que en el diseño harían el acelerómetro, el
micrófono y el caudalímetro (lo permite el profesor). Temperatura y humedad siguen
fijas hasta el paso 4 (DHT11).

| Botón / LED | Qué hace | En el diseño sería |
|---|---|---|
| Botón 1 | Cerda de pie ↔ tumbada (`posture`) | acelerómetro |
| Botón 2 | Chillido de lechón | micrófono |
| Botón 3 | Empieza / para una bajada de agua | caudalímetro |
| Botón 4 | Libre (paso 4: simular fallo del DHT11) | — |
| LED1 | Móvil conectado | — |
| LED2 | Alarma de aplastamiento | — |
| LED3 | Cerda tumbada | — |
| LED4 | Parpadea: el programa funciona | — |

Parámetros de simulación (decididos 04/11; valores de demo, **no medidas reales**):
- Ventana del chillido: **30 s** después de tumbarse. Imita el diseño, donde el
  micrófono solo escucha un rato después de que el acelerómetro detecte que se tumba.
- La alarma se apaga cuando la cerda **se levanta** (el granjero ha ido a mirar).
- Agua: base **400 mL/min** ± 15 de ruido aleatorio; con la bajada activa pierde
  **20 mL/min cada 2 s**; por debajo de **200 mL/min** → `flags bit1` (poca agua).

Cómo funciona el código (nueva sección 4 de `main.c`):
- **Interrupciones**: `dk_buttons_init(button_changed)` configura los pines de los
  botones con interrupción GPIO. Al pulsar, el procesador salta a una rutina de la
  librería de Nordic, que programa un pequeño "escaneo" en la *system workqueue*
  (un hilo del sistema). Ese escaneo quita los rebotes del contacto y llama a
  **nuestra** función `button_changed()`. Por tanto, nuestro código no corre dentro
  de la interrupción, sino en un hilo normal (por eso podemos usar `printk`).
  Ventaja frente a mirar los botones en un bucle (*polling*): el procesador no
  gasta tiempo preguntando y la reacción es inmediata.
- `button_changed()` recibe dos máscaras de bits: qué botones están pulsados y
  cuáles han cambiado. `pressed = button_state & has_changed` → solo los recién pulsados.
- `update_water()` se llama cada 2 s desde el bucle, haya móvil o no.
- `build_frame()` calcula `flags` con `|=` y `1 << n` (poner a 1 el bit n).
- Las variables de estado (`lying`, `crush_alarm`…) las escribe el hilo de los
  botones y las lee el bucle principal. No hace falta un *mutex*: son variables
  simples de 32 bits o menos, y el Cortex-M4 las lee/escribe de una sola vez.
- FLASH 138344 B (13,2 %), RAM 23668 B (9,0 %).

### 13. Prueba del paso 3 con la app ✔
- 240 tramas seguidas en ~8 min (seq 0 → 239): **0 perdidas**.
- Tumbada + chillido dentro de la ventana → `CRUSH ALARM` (a 15,5 s y 16,0 s) →
  banner rojo en la app. Levantarse → alarma borrada.
- Chillido con la cerda de pie, o con más de 30 s tumbada → `Scream ignored`.
- Bajada de agua → por debajo de 200 mL/min sale `lowWater` y el banner ámbar.
  En las 240 tramas, el bit `lowWater` coincide siempre con `agua < 200`.
- Extra: al reflashear, `seq` volvió a 0 con la app conectada. El contador de la app
  lo trató como reinicio de la placa (no como pérdida): por eso "Packets received"
  (589) es mayor que "Last seq" (147) y las pérdidas siguen a 0.
- Evidencias: `2026-11-04_step3_board_uart.log`, `2026-11-04_step3_app_logcat.log`,
  `2026-11-04_step3_app_crush_alarm.jpg`, `2026-11-04_step3_app_low_water.jpg`,
  `2026-11-04_step3_app_normal.jpg`, `2026-11-04_step3_board_leds_alarm.jpg` (foto de la
  placa en alarma: LED1 conectado, LED2 alarma, LED3 tumbada; LED4 parpadea).

### 14. Firmware paso 4: sensor real DHT11 (DFR0067)
Cableado: señal → **P0.03** (A0 del conector Arduino), VCC → **VDD** (~3 V de la
placa, no 5 V: los pines del nRF52840 no aguantan 5 V), GND → GND.
No tenemos hoja de datos del DFR0067 (TODO si la encontramos).

Cómo se hace en Zephyr:
- **Devicetree overlay** `firmware/boards/nrf52840dk_nrf52840.overlay`: el
  *devicetree* es la descripción del hardware conectado; el overlay le añade
  "hay un DHT11 (`aosong,dht`) en P0.03". El build lo usa solo porque el nombre
  coincide con la placa. `GPIO_ACTIVE_LOW` (el sensor habla bajando la línea) y
  `GPIO_PULL_UP` (resistencia interna que mantiene la línea alta en reposo).
- `prj.conf`: `CONFIG_SENSOR=y`. El driver `CONFIG_DHT` se activa solo al ver el overlay.
- `main.c` §5 `read_dht()`, cada 2 s:
  `sensor_sample_fetch()` hace la lectura (el driver baja el pin 18 ms como señal
  de inicio y mide los pulsos de respuesta: 40 bits + checksum; ~22 ms en total) y
  `sensor_channel_get()` devuelve temperatura y humedad como `val1 + val2/10⁶`.
  Con el DHT11 el driver solo da **enteros** (24 °C, 64 %) → la app muestra 24.0.
- **Botón 4** activa/desactiva un fallo simulado (para probar el flag bit2).

### 15. Problema: lecturas fallidas del DHT11 y cómo lo resolvimos
- Primera prueba: ~**27 %** de lecturas fallan (`-EIO` = checksum o tiempos mal)
  con la placa solo anunciándose, y **56 %** con el móvil conectado.
- Causa (hipótesis, no medida directamente): el driver mide pulsos de microsegundos
  "a mano"; si el Bluetooth interrumpe en mitad de la lectura, los tiempos salen
  mal. Bloquear interrupciones (`CONFIG_DHT_LOCK_IRQS`) se descartó: el proyecto
  usa `CONFIG_ZERO_LATENCY_IRQS=y`, así que las interrupciones de radio de Nordic
  no se pueden bloquear y además podríamos romper el BLE.
- **Solución 1 (opción A)**: si falla 1 o 2 veces seguidas, se reenvía el
  **último valor bueno**; solo con **3 fallos seguidos** (~6 s) se envía
  temp = hum = 0 con `flags bit2`. Al arrancar el contador empieza "lleno" para
  no enviar 0 °C como si fuera real antes de la primera lectura buena.
- **Solución 2: intervalo de conexión más largo** (`prj.conf`). El intervalo de
  conexión es cada cuánto hablan placa y móvil por radio estando conectados. Por
  defecto la placa pedía 30-50 ms; ahora pide **400-500 ms** (enviamos una trama
  cada 2 s, no hace falta más). El *supervision timeout* (tiempo sin paquetes para
  dar la conexión por perdida) sube a 4 s porque BLE exige > 2 × intervalo.
  El móvil decide al final: aceptó **480 ms** (se ve en el log
  `Connection parameters: interval 480.00 ms`). Al conectar, Android usa primero
  7,5 ms y 30 ms para descubrir servicios rápido; la placa pide el cambio a los 5 s.
  Ventaja extra: menos eventos de radio = **menos consumo** (gestión de energía).

| | Intervalo 30 ms | Intervalo 480 ms |
|---|---|---|
| Lecturas fallidas | 56 % | **23 %** (14 de 62) |
| Máx. fallos seguidos | ≥ 3 a menudo | **2** |
| Falsos "Sensor error" en la app | muchos | **0** |

### 16. Pruebas del paso 4 ✔
- **Sensor vivo**: soplando, la humedad subió de 56 % a **95 %** y la temperatura
  de 24 a **26 °C**.
- **Fallo simulado (Botón 4)**: tras 3 lecturas la app muestra "Sensor error".
- **Reconexión**: RESET de la placa → la app detecta `LINK_SUPERVISION_TIMEOUT` y
  se reconecta sola en **3,7 s** (reintenta cada 3 s).
- Evidencias: `2026-11-04_step4_board_uart_interval30ms.log`,
  `2026-11-04_step4_app_logcat_interval30ms.log` (fallos + reconexión),
  `2026-11-04_step4_board_uart_interval480ms.log`,
  `2026-11-04_step4_app_logcat_interval480ms.log` (con la solución),
  `2026-11-04_step4_app_dht11_breath_95pct.jpg`,
  `2026-11-04_step4_app_sensor_error_simulated.jpg`.
