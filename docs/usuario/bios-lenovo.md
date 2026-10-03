# Configuración de BIOS y sistema operativo para el encendido remoto (Lenovo ThinkStation P3 Ultra SFF G2)

Historia: MESP-US-0030. Equipo de referencia: **Lenovo ThinkStation P3 Ultra SFF G2** con Linux (Pop!_OS / Ubuntu 24.04). Su NIC cableada es `enp128s31f6` (MAC `fc:9d:05:18:ee:32`).

El dongle enciende el PC por dos vías:

1. **Teclado USB (HID)**. En suspensión (S3) basta con el *remote wakeup* USB estándar. En hibernación (S4) y apagado (S5) la BIOS tiene que mantener alimentado y vigilado el puerto USB.
2. **Wake-on-LAN (WOL)**: paquete mágico por la red local a las MACs que el agente comunica al dongle. Es el respaldo del método por defecto `hid_then_wol`.

> **Estado de validación.** Esta guía todavía **no está validada en el equipo**. El spike MESP-US-0002 (matriz S3/S4/S5 × puerto × BIOS) sigue pendiente y no existe `docs/analisis/spike-wake.md`. Los nombres de los menús que aparecen abajo son los habituales en las BIOS de Lenovo ThinkStation/ThinkCentre (fuentes al final), pero **no se han comprobado en la P3 Ultra SFF G2**. Cuando hagas la prueba, apunta el nombre real de cada opción en la columna «Nombre real en este equipo» y el resultado en [`docs/qa/e2e-v1.md`](../qa/e2e-v1.md).

## 1. Entrar en la BIOS

1. Reinicia el PC y pulsa **F1** repetidamente cuando aparezca el logo de Lenovo. Así lo indica la guía de usuario de la P3 Ultra SFF G2 ([ManualsLib][ug-g2]).
2. **F10** guarda y sale. **F9** carga los valores por defecto: no la uses salvo que quieras deshacer todos los cambios.
3. **Antes de cambiar nada, haz una foto de cada pantalla del menú *Power*** para poder volver al estado anterior.

## 2. Ajustes de BIOS

| # | Qué buscar (nombre habitual en Lenovo) | Menú probable | Valor recomendado | Para qué sirve | Seguridad del dato | Nombre real en este equipo |
|---|---|---|---|---|---|---|
| 1 | **Enhanced Power Saving Mode** (modo ErP LPS / «Deep Sleep» / «Energy Star») | Power | **Disabled** | Con ErP activo, en S4/S5 el equipo corta la alimentación de los USB y de la NIC, y solo arranca con el botón, la alarma o tras un corte de luz. Lenovo indica que, si se activa, hay que desactivar Wake on LAN ([Lenovo ErP][erp]). | Opción documentada en Lenovo; falta confirmar su nombre en la P3 Ultra G2 | |
| 2 | **Automatic Power On → Wake on LAN** | Power → Automatic Power On | **Primary** o **Enabled** (según las opciones que ofrezca) | Respaldo WOL desde S3/S4/S5 | Opción documentada en ThinkCentre/ThinkStation ([manual ThinkCentre][tc-hmm]) | |
| 3 | **Wake from keyboard / USB** («Wake Up on USB», «USB Wake Support», «Keyboard Power On») | Power → Automatic Power On, o Devices → USB Setup | **Enabled** | Despertar con el teclado HID del dongle desde S4/S5. Desde S3 lo controla el sistema operativo (§3.1) | **Desconocido**: no se ha encontrado documentación pública de esta opción en la P3 Ultra G2. Puede no existir | |
| 4 | **Always On USB** / «USB power in S4/S5» / «Charge in Battery/Off mode» | Devices → USB Setup o Power | **Enabled** | Mantiene los 5 V del puerto USB con el PC apagado: el dongle sigue conectado a Wi-Fi y puede mandar el WOL y la señal de *resume*. Sin esta opción, en S5 el dongle se apaga y **no puede encender nada** | **Desconocido** en este modelo. Algunos equipos lo limitan a un puerto concreto (marcado con un rayo o una batería) | |
| 5 | **After Power Loss** | Power | **Last State** (recomendado) o **Power On** | Qué hace el PC al volver la corriente tras un corte. Con *Last State* vuelve a como estaba; *Power Off* exige pulsar el botón | Opción documentada ([Lenovo ErP][erp], [foro Lenovo][forum-apl]) | |
| 6 | **Smart Power On** | Power → Smart Power On | Indiferente (déjalo como esté) | Encender con Alt+P desde un teclado USB *de Lenovo*. No se aplica al dongle | Documentada en la guía de la P3 Ultra SFF G2 ([ManualsLib][ug-g2]) | |
| 7 | **Fast Boot / Quick Boot** (Startup → Boot Mode: Quick / Diagnostics) | Startup | **Diagnostics** (o Fast Boot desactivado) mientras haces las pruebas | Con el arranque rápido la BIOS puede omitir la inicialización USB. No afecta al despertar, pero sí a ver el dongle en la BIOS | Probable; falta confirmar | |
| 8 | **Wake Up on Alarm** | Power → Automatic Power On | Disabled (salvo para las pruebas de §4.3) | Encendido programado. Útil como red de seguridad en las pruebas | Documentada en otros Lenovo ([Lenovo ErP][erp]) | |

**Windows** (solo si el PC arranca Windows): desactiva el *Inicio rápido* (Panel de control → Opciones de energía → Elegir el comportamiento de los botones → «Activar inicio rápido»). Con él activado, «Apagar» es en realidad una hibernación híbrida y el WOL y el despertar por USB se comportan de otra forma. En el Administrador de dispositivos, en el teclado HID «MicroESP» → Administración de energía, marca «Permitir que este dispositivo reactive el equipo». En la NIC marca «Solo permitir un paquete mágico para reactivar el equipo».

### Puerto USB recomendado

Usa un **puerto trasero de la placa base** (no un hub ni el frontal) y, si alguno está marcado como *Always On* o con un icono de carga, ese. **No se sabe todavía** qué puertos de la P3 Ultra SFF G2 mantienen los 5 V en S5. Para comprobarlo, apaga el PC (S5) con el dongle enchufado: si la pantalla o el LED del dongle siguen encendidos, el puerto está alimentado (también se puede medir con un multímetro USB). Apunta el resultado en el plan E2E.

## 3. Linux

### 3.1 Permitir que el dongle despierte el equipo (S3)

El instalador del agente (`sudo agent/deploy/install.sh`) ya instala la regla `/etc/udev/rules.d/99-microesp.rules` ([`agent/deploy/udev/99-microesp.rules`](../../agent/deploy/udev/99-microesp.rules)), que hace `ATTR{power/wakeup}="enabled"` sobre el dispositivo USB `303a:4002`. Para comprobarlo:

```bash
# Debe mostrar "enabled" para el dongle
for d in /sys/bus/usb/devices/*; do
  [ "$(cat "$d/idVendor" 2>/dev/null)" = 303a ] && echo "$d: $(cat "$d/product") wakeup=$(cat "$d/power/wakeup")"
done
# El controlador xHCI también debe poder despertar al sistema (columna "*enabled")
grep -i xhc /proc/acpi/wakeup
```

Si el controlador `XHC` aparece como `*disabled`, actívalo con `echo XHC | sudo tee /proc/acpi/wakeup`. Ten en cuenta que esa orden **alterna** el estado y no persiste tras reiniciar. Si hace falta, crea una unidad systemd o una regla udev para el controlador PCI (`ATTR{power/wakeup}="enabled"` sobre el dispositivo xHCI).

El firmware informa del fallo `hid_not_armed` (bit 2 del DP 114) si el host suspende el bus sin armar el *remote wakeup*: indica que esta parte no está bien configurada.

### 3.2 Wake-on-LAN en la NIC

Ver y activar con `ethtool` (no persiste tras reiniciar):

```bash
sudo ethtool enp128s31f6 | grep -i wake     # "Supports Wake-on: pumbg"  "Wake-on: d" (desactivado)
sudo ethtool -s enp128s31f6 wol g           # g = paquete mágico
sudo ethtool enp128s31f6 | grep -i 'Wake-on:'   # debe decir "Wake-on: g"
```

Forma persistente con NetworkManager (Pop!_OS / Ubuntu):

```bash
nmcli -f NAME,DEVICE connection show                 # nombre de la conexión de enp128s31f6
sudo nmcli connection modify "<conexión>" 802-3-ethernet.wake-on-lan magic
sudo nmcli connection up "<conexión>"
nmcli -g 802-3-ethernet.wake-on-lan connection show "<conexión>"   # → magic
```

Con systemd-networkd, usa en su lugar un fichero `.link` con `WakeOnLan=magic`.

El dongle envía el paquete mágico a las MACs que recibe del agente en el `hello` (como máximo 4, y solo de NIC físicas). Por eso **el agente tiene que haber conectado al menos una vez** después de emparejar. Para comprobarlo, mira la línea `agent:` de `!status` en la CLI del dongle (con el agente parado): `macs=` debe ser ≥ 1. Para probar el WOL desde otro equipo de la misma red: `wakeonlan fc:9d:05:18:ee:32` o `etherwake -i <if> fc:9d:05:18:ee:32`.

> El WOL desde S5 depende de que la BIOS deje alimentada la NIC (ErP desactivado, Wake on LAN activado) y de que el driver (`e1000e` en las NIC Intel I219) deje armado el WOL al apagar. Algunas versiones de NetworkManager o del driver lo desactivan al apagar. Si falla desde S5 pero funciona desde S3, este es el primer sitio que revisar.

## 4. Cómo probarlo sin riesgo (primero S3, luego S5)

Prepara siempre una vía de recuperación: el **botón de encendido físico** sigue funcionando en todos los casos. No hagas estas pruebas en remoto sin alguien al lado del PC.

### 4.1 Comprobaciones previas

1. El dongle está enchufado en el puerto elegido y conectado a Smart Life (el DP `pc_state` muestra `on`).
2. El agente está activo y emparejado: `systemctl status microesp-agent` y `agent_online = true` en la app.
3. El método de encendido (DP 109 `wake_method`) es `hid_then_wol` (valor por defecto).
4. El dongle tiene las MACs del PC (el agente conectó después de emparejar).

### 4.2 Suspensión (S3)

```bash
# Red de seguridad: el RTC despierta el equipo a los 180 s si el dongle no lo hace
sudo rtcwake -m mem -s 180
```

Mientras está suspendido (el DP `pc_state` pasa a `sleep`), pulsa **Encender** en la app o haz una **doble pulsación** en el botón del dongle. El PC debe despertar en pocos segundos, mucho antes de los 180 s. Para probar sin red de seguridad, usa `systemctl suspend`.

Resultado esperado: `last_result = wake_sent` → `pc_state` pasa a `booting`/`on`. Si no despierta, revisa §3.1 (`power/wakeup`, `/proc/acpi/wakeup`) y el bit `hid_not_armed`.

### 4.3 Hibernación (S4), si está configurada

```bash
systemctl hibernate
```

Muchas instalaciones de Pop!_OS/Ubuntu no tienen la hibernación configurada (requiere swap suficiente y `resume=`). Si no lo está, salta este caso y márcalo como «N/A» en el plan E2E.

### 4.4 Apagado (S5)

1. Primero solo **WOL**: pon el DP 109 en `wol`, apaga con `systemctl poweroff` y, desde la app, pulsa **Encender**. Si el dongle se apaga junto con el PC (puerto sin alimentación en S5), el WOL no podrá salir del dongle: pruébalo antes desde otro equipo con `wakeonlan`.
2. Después solo **HID**: DP 109 = `hid`, apaga y pulsa **Encender**. El firmware fuerza la señal de *resume* 3 veces con 2 s de separación. Solo funciona si la BIOS vigila el puerto en S5 (ajustes 3 y 4 de la tabla).
3. Por último, el método por defecto `hid_then_wol`.
4. Si a los 120 s el bus USB no se ha montado, el dongle pone `last_result = wake_failed`. En ese caso enciende con el botón físico y revisa la tabla de §2.

Red de seguridad opcional para S5: `sudo rtcwake -m off -s 300` (necesita que la BIOS permita despertar por RTC, ajuste 8; no se ha verificado en este equipo).

## 5. Qué falta confirmar

- [ ] Nombre exacto de cada opción del §2 en la BIOS de la P3 Ultra SFF G2 y versión de la BIOS (`sudo dmidecode -s bios-version`).
- [ ] Si existe alguna opción de «USB wake»/«keyboard power on» desde S4/S5.
- [ ] Qué puertos USB dan 5 V en S5.
- [ ] Matriz de resultados S3/S4/S5 × HID/WOL (MESP-US-0002, [`docs/qa/e2e-v1.md`](../qa/e2e-v1.md)).

## Fuentes

- Lenovo ThinkStation P3 Ultra SFF G2, guía de usuario (entrar en la BIOS con F1, Power → Smart Power On): [ManualsLib][ug-g2]
- Lenovo, «Enabling or disabling the ErP LPS compliance mode» (Power → Enhanced Power Saving Mode; desactivar Wake on LAN si se activa; Wake Up on Alarm, After Power Loss): [download.lenovo.com][erp]
- Lenovo ThinkCentre, manual de mantenimiento (Automatic Power On, Wake on LAN): [ManualsLib][tc-hmm]
- Foro de Lenovo, «After Power Loss»: [forums.lenovo.com][forum-apl]
- Especificaciones de la ThinkStation P3 Ultra SFF Gen 2 (PSREF): [psref.lenovo.com][psref]

[ug-g2]: https://www.manualslib.com/manual/3960124/Lenovo-Thinkstation-P3-Ultra-Sff-G2.html
[erp]: https://download.lenovo.com/pccbbs/pubs/p330_tiny/html_en/en/Enabling_or_disabling_the_ErP_LPS_compliance_mode_(topic)_T0000763260.html
[tc-hmm]: https://www.manualslib.com/manual/701412/Lenovo-Thinkcentre-Edge.html?page=173
[forum-apl]: https://forums.lenovo.com/t5/ThinkCentre-A-E-M-S-Series/After-Power-Loss/m-p/5248695
[psref]: https://psref.lenovo.com/syspool/Sys/PDF/ThinkStation/ThinkStation_P3_Ultra_SFF_Gen_2/ThinkStation_P3_Ultra_SFF_Gen_2_Spec.pdf
