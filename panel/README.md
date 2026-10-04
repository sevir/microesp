# Panel MicroESP (Ray)

Panel MiniApp para Smart Life hecho con [Ray](https://developer.tuya.com/en/miniapp/develop/ray/guide/start/quick-start). Sustituye al panel estándar del producto con el diseño del lienzo «Panel MicroESP»: estado del PC, encender, apagar y reiniciar con confirmación y cuenta atrás cancelable, telemetría y ajustes.

## Cómo habla con el dispositivo

El producto es **TuyaLink**, así que el panel usa el modelo de cosa y no los DPs clásicos:

| Qué | API de Ray | Detalle |
|---|---|---|
| Valores iniciales | `getDeviceInfo` | `dpCodes` (por código) y `dps` (por abilityId 101–114) |
| Cambios | `subscribeReceivedThingModelMessage` + `onReceivedThingModelMessage` | Mensajes de propiedad (`type: 0`), valores sueltos o `{value, time}` |
| Cambios (respaldo) | `onDpDataChange` | Por si la app también los entrega como DPs |
| Escritura | `publishThingModelMessage` | `type: 0`, `payload: {<código>: valor}` = `thing/property/set` |
| En línea | `getDeviceInfo().isOnline`, `onDeviceOnlineStatusUpdate` | Estado del dongle en la nube |

Los códigos, abilityIds, rangos y valores por defecto salen de [`firmware/schema/dp.json`](../firmware/schema/dp.json). `npm test` comprueba que [`src/device/model.ts`](src/device/model.ts) coincide con ese esquema.

Semántica que respeta el panel (ver `dp.json`):

- `power_on` es un pulsador: el panel envía `true` y el dongle lo devuelve a `false`.
- `power_off` / `reboot` a `true` inician la cuenta atrás de `cmd_countdown`. Enviar `false` mientras cuenta la cancela. El dongle no informa del tiempo restante, así que el panel lo estima en local desde que ve el `true`.
- Apagar y reiniciar solo se activan con `pc_state = on` y `agent_online = true`. El panel siempre pide confirmación antes.
- La telemetría llega en décimas de porcentaje (`184` → 18,4 %).
- `fault` es una máscara de bits. Cada bit activo muestra un aviso.

## Estructura

```
panel/
├── project.tuya.json        # Kits y versión base de la MiniApp (devMode: ray)
├── src/
│   ├── app.tsx
│   ├── routes.config.ts     # Una sola página: pages/home
│   ├── device/
│   │   ├── model.ts         # Modelo de cosa: tipos, normalización, reglas (sin dependencias)
│   │   └── useMicroEsp.ts   # Hook: estado en vivo y escritura por TuyaLink
│   ├── strings/index.ts     # Textos es/en según el idioma de la app
│   ├── components/          # Icon (SVG como data URI), ConfirmSheet
│   ├── pages/home/          # Panel
│   └── variables.less       # Paleta
└── test/model.test.mjs      # Tests del modelo contra dp.json
```

> `src/devices` y `src/i18n` son nombres que raypack compila aparte con otra resolución de módulos, y la compilación falla. Por eso aquí se llaman `device` y `strings`.

## Desarrollo

Requisitos: Node ≥ 22.6, Tuya MiniApp IDE y una cuenta de la Tuya Developer Platform. `project.tuya.json` necesita `baseversion` ≥ 2.27.0 o el IDE no compila.

```bash
cd panel
npm install
npm test             # modelo frente a firmware/schema/dp.json
npm run typecheck
npm run build:tuya   # genera dist/tuya
```

### El IDE en Linux (Wine)

Tuya solo publica el MiniApp IDE para Windows y macOS. En Linux funciona la versión de Windows bajo Wine con estos ajustes (montaje local en `/www/MicroESP/tools/tuya-ide/`, ver su `README.txt`):

- La aplicación se extrae del instalador NSIS (`$PLUGINSDIR/app-64.7z`) a un prefijo propio. El instalador falla bajo Wine.
- Node para Windows en el prefijo y en su `PATH`.
- El IDE compila lanzando `npx ray build` en una terminal PowerShell, y en Wine `powershell.exe` no hace nada. Hay que parchear `rayBuild()` en `@ark/miniapp-compiler` para que ejecute `node node_modules/@ray-js/cli/bin/ray build …` directamente.
- `node_modules` instalado en Linux solo trae los binarios nativos de Linux. Tras cada `npm install`, ejecuta `npm run ide:win-natives` para añadir los de Windows (esbuild, lightningcss, oxc, tailwind oxide).

Las carpetas `typings/tuya*` las genera el IDE al importar el proyecto y no se versionan.

## Probarlo en el móvil

1. En el IDE, **Account** (arriba a la derecha) muestra un QR. Escanéalo con Smart Life (**+ → Escanear**) usando la cuenta dueña del dispositivo.
2. **Plugins → Panel Tools → debug real device** y elige el dispositivo.
3. **Preview** genera otro QR. Escanéalo con Smart Life y se abre el panel con el dispositivo real, sin cambiar el panel que ve el producto.

Apagar y Reiniciar actúan de verdad: para probar la orden, confirma y cancela durante la cuenta atrás.

## Publicarlo en Smart Life

1. En la Smart MiniApp Developer Platform crea una **Panel MiniApp**.
2. En el MiniApp IDE importa la carpeta `panel/` y vincula el **producto** TuyaLink de MicroESP y la MiniApp del paso 1.
3. Sube la versión desde el IDE. En la plataforma de MiniApps, en **gestión de versiones**, pide la revisión y, una vez aprobada, actívala como versión online.
4. En la plataforma de IoT, en el producto, cambia el panel por esta MiniApp.

Comprobado con el dispositivo real: `getDeviceInfo` entrega las propiedades en `dps` con claves de abilityId (`"101"`…`"114"`), que el panel mezcla con `mergeDps`.

Pendiente de comprobar:

- si `getDeviceInfo` rellena también `dpCodes` en productos TuyaLink;
- la forma exacta del `payload` de `onReceivedThingModelMessage`. El modelo acepta valores sueltos y `{value, time}`;
- si la publicación exige verificar la organización, como pasó con las licencias de TuyaOS.
