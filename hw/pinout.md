# Pinout Pocket-Dongle-S3 (MESP-US-0001)

Clon de LilyGO T-Dongle-S3. ESP32-S3 (QFN56) rev v0.2, PSRAM 8 MB octal embebida, flash 16 MB quad, MAC `90:70:69:f6:62:dc`.
Spike: `hw/spikes/pinout/` (ESP-IDF v5.4). El mismo código corre también dentro del firmware TuyaOpen del spike `hw/spikes/tuya-usb/` (componente `pinout_probe`), que es el que queda grabado en la placa.

## Estado

| Función | GPIO (referencia T-Dongle-S3) | Estado | Evidencia |
|---|---|---|---|
| Flash 16 MB / PSRAM 8 MB octal | — | **Verificado por software** | esptool `flash-id`; log `flash 16 MB, PSRAM 8192 KB` con `SPIRAM_MODE_OCT` |
| LCD SDA (MOSI) | 3 | Pendiente de confirmación visual | sin lectura posible (ver nota) |
| LCD SCL | 5 | Pendiente visual | |
| LCD CS | 4 | Pendiente visual | |
| LCD DC | 2 | Pendiente visual | |
| LCD RST | 1 | Pendiente visual | |
| Retroiluminación (activa a nivel bajo) | 38 | Pendiente visual | |
| LED RGB | candidatos WS2812 en 40/39/48/38/21, APA102 en 40/39 | Pendiente visual | |
| Botón BOOT | 0 | Parcial: nivel en reposo = 1 (pull-up) leído en todos los arranques; falta una pulsación real | log `BUTTON GPIO0 raw level=1` |
| TF (SDMMC 4 bits) CLK12 CMD16 D0 14 D1 17 D2 21 D3 18 | | No verificable sin tarjeta | `sdmmc_init_ocr ... 0x107` (timeout: sin tarjeta o pines distintos) |

Nota LCD: se intentó verificar el cableado leyendo `RDDID`/`RDDST`/`RDDMADCTL` del ST7735 en modo 3 hilos (SDA bidireccional, bit-bang) con los pines de referencia y con las 120 permutaciones (MOSI, SCLK, CS, DC) de GPIO 1–5, con pull-up y pull-down. En ningún caso el panel conduce la línea (lectura idéntica a la del pull): el módulo no permite lectura por SDA. El cableado del LCD solo se puede confirmar viendo la pantalla.

Observación: en dos arranques del spike aislado GPIO0 se leyó a 0 de forma continua ~40 s (¿botón pulsado por alguien?). No se ha vuelto a reproducir; en los demás arranques lee 1 de forma estable.

## Configuración de pantalla en prueba

ST7735S 80×160 usado en horizontal (160×80): `MADCTL=0x68` (MX|MV|BGR), inversión ON (`0x21`), RGB565, desplazamiento columna 1 / fila 26 (equivale a col 26 / fila 1 en vertical, el valor típico de los paneles 0,96" en RAM 132×162). SPI2 a 26 MHz, modo 0.

Patrón: marco blanco de 1 px en el borde exterior; cuadrados en las esquinas (sup-izq **rojo**, sup-der **verde**, inf-izq **azul**, inf-der **amarillo**); texto **"MicroESP"** grande arriba; cuatro barras etiquetadas **R G B W**; línea cian con el paso actual del ciclo; línea magenta `BTN:n` con el contador de pulsaciones.

## Ciclo de prueba (se repite sin fin, ~30 s)

La línea cian de la pantalla dice en todo momento qué se está probando (y el log lo registra con marca de tiempo):

| Paso | Duración | Qué se hace |
|---|---|---|
| `BL38=0 (LOW)` | 3 s | GPIO38 a 0 |
| `BL38=1 (HIGH)` | 3 s | GPIO38 a 1 |
| `WS2812 G40 RED/GREEN/BLUE` | 3 × 0,7 s | WS2812 por RMT en GPIO40 |
| `WS2812 G39 …` | 2,1 s | ídem GPIO39 |
| `WS2812 G48 …` | 2,1 s | ídem GPIO48 |
| `WS2812 G38 …` | 2,1 s | ídem GPIO38 (la retroiluminación parpadeará) |
| `WS2812 G21 …` | 2,1 s | ídem GPIO21 |
| `APA D40 C39 …` | 2,1 s | APA102 DI=40 CLK=39 (bit-bang) |
| `APA D39 C40 …` | 2,1 s | APA102 DI=39 CLK=40 |
| `IDLE 5s` | 5 s | nada |

## Checklist para el usuario (confirmación visual)

Con el dongle enchufado al PC (firmware actual: TuyaOpen + TinyUSB + este ciclo):

1. **Pantalla**: ¿se ve el patrón?
   - ¿Se ven las **4 líneas** del marco blanco (arriba, abajo, izq., der.)? Si falta una y sobra basura en el lado opuesto, el desplazamiento está mal: anotar qué lado.
   - ¿El texto "MicroESP" se lee bien (no en espejo, no al revés)?
   - ¿Esquina sup-izq **roja** y barra "R" **roja**? (si salen azules → orden RGB/BGR invertido).
   - ¿Fondo **negro**? (si es blanco/claro → inversión al revés).
2. **Retroiluminación**: durante `BL38=0` la pantalla debe verse; durante `BL38=1` debe apagarse (activa a nivel bajo). Anotar si es al revés o si no cambia.
3. **LED**: anotar en qué paso (texto de la línea cian) se enciende el LED y de qué color: p. ej. "WS2812 G40: rojo→verde→azul correcto" o "APA D40 C39: se enciende".
4. **Botón**: pulsar BOOT brevemente; el contador `BTN:` de la pantalla debe incrementarse. **No** mantenerlo ≥2 s: con el firmware TinyUSB eso pone la placa en modo descarga (se recupera desenchufando y enchufando).
5. Opcional **TF**: si se inserta una microSD (FAT), se puede volver a probar con el spike aislado `hw/spikes/pinout` (monta en el arranque y lista la raíz).
6. Foto de la placa por ambas caras para este documento (criterio de aceptación de US-0001).

Los resultados se rellenan en la tabla de "Estado".
