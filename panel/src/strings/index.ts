import { getSystemInfoSync } from '@ray-js/ray';
import type { Fault, LastResult, PcState, WakeMethod } from '@/device/model';
import { splitUptime } from '@/device/model';

interface Strings {
  title: string;
  moreOptions: string;
  dongleOnline: string;
  dongleOffline: string;
  status: string;
  commandRunning: string;
  state: Record<PcState, string>;
  sub: Record<PcState, string>;
  bootingSub: (method: string) => string;
  uptimeSub: (uptime: string) => string;
  shuttingDown: string;
  rebooting: string;
  countingSub: (s: number) => string;
  powerOn: string;
  wakeUp: string;
  powerOff: string;
  reboot: string;
  cancel: string;
  needsAgent: string;
  performance: string;
  teleLive: string;
  teleIdle: string;
  cpu: string;
  memory: string;
  diskFree: string;
  settings: string;
  wakeMethod: string;
  wakeLabel: Record<WakeMethod, string>;
  wakeHelp: Record<WakeMethod, string>;
  countdown: string;
  countdownHelp: string;
  lessSeconds: string;
  moreSeconds: string;
  agent: string;
  agentOnline: string;
  agentOffline: string;
  lastCommand: string;
  last: Record<LastResult, string>;
  faultTitle: Record<Fault, string>;
  faultText: Record<Fault, string>;
  confirmOffTitle: string;
  confirmRebootTitle: string;
  confirmText: (kind: 'off' | 'reboot', countdown: number, agentOnline: boolean) => string;
  sendFailed: string;
  schedule: string;
  scheduleNote: string;
  scheduleAction: string;
  when: string;
  whenIn: string;
  whenAt: string;
  runAt: (clock: string) => string;
  atTime: string;
  days: string;
  daysHelp: string;
  countdownNote: string;
  scheduleBtn: string;
  scheduled: (clock: string) => string;
  scheduleFailed: string;
  upcoming: string;
  nothingScheduled: string;
  removeFailed: string;
  once: string;
  everyDay: string;
  weekdays: string[];
  weekdayInitials: string[];
  inTime: (t: string) => string;
  formatUptime: (seconds: number) => string;
  decimalSep: string;
}

const es: Strings = {
  title: 'Mi PC',
  moreOptions: 'Más opciones',
  dongleOnline: 'dongle en línea',
  dongleOffline: 'dongle sin conexión',
  status: 'Estado',
  commandRunning: 'Orden en curso',
  state: {
    off: 'Apagado',
    sleep: 'Suspendido',
    booting: 'Arrancando',
    on_no_agent: 'Encendido',
    on: 'Encendido',
    unknown: 'Sin datos',
  },
  sub: {
    off: 'Pulsa Encender para arrancarlo desde aquí.',
    sleep: 'En reposo. Se despierta en un par de segundos.',
    booting: '',
    on_no_agent: 'El agente del PC no responde.',
    on: '',
    unknown: 'El dongle aún no ha informado del estado.',
  },
  bootingSub: (m) => `Señal enviada (${m}). Esperando al agente.`,
  uptimeSub: (u) => `Encendido desde hace ${u}`,
  shuttingDown: 'Apagando',
  rebooting: 'Reiniciando',
  countingSub: (s) => `En ${s} s. Puedes cancelarlo mientras cuenta.`,
  powerOn: 'Encender',
  wakeUp: 'Despertar',
  powerOff: 'Apagar',
  reboot: 'Reiniciar',
  cancel: 'Cancelar',
  needsAgent: 'El agente del PC no responde: el dongle rechazará apagar y reiniciar.',
  performance: 'Rendimiento',
  teleLive: 'Se actualiza cada 30 s',
  teleIdle: 'Sin datos del agente',
  cpu: 'CPU',
  memory: 'Memoria',
  diskFree: 'Disco libre',
  settings: 'Ajustes',
  wakeMethod: 'Método de encendido',
  wakeLabel: { hid: 'USB', wol: 'Red', hid_then_wol: 'USB + red' },
  wakeHelp: {
    hid: 'El dongle despierta el PC como un teclado USB.',
    wol: 'Se envía un paquete Wake-on-LAN por la red local.',
    hid_then_wol: 'Primero por USB; si el PC no responde, Wake-on-LAN.',
  },
  countdown: 'Cuenta atrás',
  countdownHelp: 'Antes de apagar o reiniciar',
  lessSeconds: 'Menos segundos',
  moreSeconds: 'Más segundos',
  agent: 'Agente del PC',
  agentOnline: 'Conectado',
  agentOffline: 'Sin conexión',
  lastCommand: 'Última orden',
  last: {
    ok: 'Completada',
    wake_sent: 'Señal de encendido enviada',
    wake_failed: 'No se pudo encender',
    cmd_rejected: 'Orden rechazada',
    agent_offline: 'Agente sin conexión',
    cancelled: 'Cancelada',
  },
  faultTitle: {
    agent_lost: 'Se perdió la conexión con el agente',
    wake_failed: 'El PC no respondió al encendido',
    hid_not_armed: 'El despertar por USB no está activo',
    cloud_lost: 'El dongle perdió la conexión con la nube',
  },
  faultText: {
    agent_lost: 'Comprueba que el servicio microesp-agent está activo en el PC y que el dongle sigue conectado por USB.',
    wake_failed: 'Revisa en la BIOS el encendido por USB y Wake-on-LAN, o prueba otro método de encendido.',
    hid_not_armed: 'El PC no ha habilitado el remote wakeup del dongle. Usa Wake-on-LAN o revisa la BIOS.',
    cloud_lost: 'Los datos pueden estar desactualizados hasta que vuelva la conexión Wi-Fi.',
  },
  confirmOffTitle: '¿Apagar el PC?',
  confirmRebootTitle: '¿Reiniciar el PC?',
  confirmText: (kind, c, agentOnline) =>
    (kind === 'off' ? 'Se apagará ' : 'Se reiniciará ') +
    (c > 0 ? `tras una cuenta atrás de ${c} s, que puedes cancelar. ` : 'de inmediato. ') +
    'Guarda antes tu trabajo abierto.' +
    (agentOnline ? '' : ' El agente del PC parece desconectado, así que el dongle puede rechazar la orden.'),
  sendFailed: 'No se pudo enviar la orden',
  schedule: 'Programar',
  scheduleNote: 'Lo ejecuta la nube de Tuya',
  scheduleAction: 'Orden',
  when: 'Cuándo',
  whenIn: 'Dentro de',
  whenAt: 'A una hora',
  runAt: (c) => `Se ejecutará a las ${c}.`,
  atTime: 'Hora',
  days: 'Días',
  daysHelp: 'Sin días marcados se ejecuta una sola vez.',
  countdownNote: 'Apagar y reiniciar mantienen la cuenta atrás cancelable.',
  scheduleBtn: 'Programar',
  scheduled: (c) => `Programado a las ${c}`,
  scheduleFailed: 'No se pudo programar',
  upcoming: 'Programado',
  nothingScheduled: 'Nada programado',
  removeFailed: 'No se pudo quitar',
  once: 'una vez',
  everyDay: 'todos los días',
  weekdays: ['dom', 'lun', 'mar', 'mié', 'jue', 'vie', 'sáb'],
  weekdayInitials: ['D', 'L', 'M', 'X', 'J', 'V', 'S'],
  inTime: (t) => `en ${t}`,
  formatUptime: (s) => {
    const { d, h, m } = splitUptime(s);
    if (d > 0) return `${d} d ${h} h`;
    if (h > 0) return `${h} h ${m} min`;
    return `${m} min`;
  },
  decimalSep: ',',
};

const en: Strings = {
  ...es,
  title: 'My PC',
  moreOptions: 'More options',
  dongleOnline: 'dongle online',
  dongleOffline: 'dongle offline',
  status: 'Status',
  commandRunning: 'Command running',
  state: {
    off: 'Off',
    sleep: 'Asleep',
    booting: 'Starting',
    on_no_agent: 'On',
    on: 'On',
    unknown: 'No data',
  },
  sub: {
    off: 'Tap Power on to start it from here.',
    sleep: 'Sleeping. Wakes up in a couple of seconds.',
    booting: '',
    on_no_agent: 'The PC agent is not responding.',
    on: '',
    unknown: 'The dongle has not reported the state yet.',
  },
  bootingSub: (m) => `Signal sent (${m}). Waiting for the agent.`,
  uptimeSub: (u) => `On for ${u}`,
  shuttingDown: 'Shutting down',
  rebooting: 'Restarting',
  countingSub: (s) => `In ${s} s. You can cancel while it counts.`,
  powerOn: 'Power on',
  wakeUp: 'Wake up',
  powerOff: 'Shut down',
  reboot: 'Restart',
  cancel: 'Cancel',
  needsAgent: 'The PC agent is offline: the dongle will reject shut down and restart.',
  performance: 'Performance',
  teleLive: 'Updates every 30 s',
  teleIdle: 'No agent data',
  cpu: 'CPU',
  memory: 'Memory',
  diskFree: 'Free disk',
  settings: 'Settings',
  wakeMethod: 'Wake method',
  wakeLabel: { hid: 'USB', wol: 'Network', hid_then_wol: 'USB + net' },
  wakeHelp: {
    hid: 'The dongle wakes the PC as a USB keyboard.',
    wol: 'A Wake-on-LAN packet is sent over the local network.',
    hid_then_wol: 'USB first; Wake-on-LAN if the PC does not respond.',
  },
  countdown: 'Countdown',
  countdownHelp: 'Before shutting down or restarting',
  lessSeconds: 'Fewer seconds',
  moreSeconds: 'More seconds',
  agent: 'PC agent',
  agentOnline: 'Connected',
  agentOffline: 'Offline',
  lastCommand: 'Last command',
  last: {
    ok: 'Done',
    wake_sent: 'Wake signal sent',
    wake_failed: 'Could not power on',
    cmd_rejected: 'Command rejected',
    agent_offline: 'Agent offline',
    cancelled: 'Cancelled',
  },
  faultTitle: {
    agent_lost: 'Lost connection to the agent',
    wake_failed: 'The PC did not respond to power on',
    hid_not_armed: 'USB wake is not armed',
    cloud_lost: 'The dongle lost its cloud connection',
  },
  faultText: {
    agent_lost: 'Check that the microesp-agent service is running and the dongle is still plugged in.',
    wake_failed: 'Check USB wake and Wake-on-LAN in the BIOS, or try another wake method.',
    hid_not_armed: 'The PC has not enabled remote wakeup for the dongle. Use Wake-on-LAN or check the BIOS.',
    cloud_lost: 'Data may be stale until the Wi-Fi connection is back.',
  },
  confirmOffTitle: 'Shut down the PC?',
  confirmRebootTitle: 'Restart the PC?',
  confirmText: (kind, c, agentOnline) =>
    (kind === 'off' ? 'It will shut down ' : 'It will restart ') +
    (c > 0 ? `after a ${c} s countdown you can cancel. ` : 'right away. ') +
    'Save your open work first.' +
    (agentOnline ? '' : ' The PC agent looks offline, so the dongle may reject the command.'),
  sendFailed: 'Could not send the command',
  schedule: 'Schedule',
  scheduleNote: 'Run by the Tuya cloud',
  scheduleAction: 'Command',
  when: 'When',
  whenIn: 'In',
  whenAt: 'At a time',
  runAt: (c) => `It will run at ${c}.`,
  atTime: 'Time',
  days: 'Days',
  daysHelp: 'With no day selected it runs once.',
  countdownNote: 'Shut down and restart keep the cancelable countdown.',
  scheduleBtn: 'Schedule',
  scheduled: (c) => `Scheduled for ${c}`,
  scheduleFailed: 'Could not schedule',
  upcoming: 'Scheduled',
  nothingScheduled: 'Nothing scheduled',
  removeFailed: 'Could not remove',
  once: 'once',
  everyDay: 'every day',
  weekdays: ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'],
  weekdayInitials: ['S', 'M', 'T', 'W', 'T', 'F', 'S'],
  inTime: (t) => `in ${t}`,
  formatUptime: (s) => {
    const { d, h, m } = splitUptime(s);
    if (d > 0) return `${d} d ${h} h`;
    if (h > 0) return `${h} h ${m} min`;
    return `${m} min`;
  },
  decimalSep: '.',
};

function pickLanguage(): Strings {
  try {
    const lang = String(getSystemInfoSync().language || '').toLowerCase();
    return lang.startsWith('es') ? es : en;
  } catch {
    return es;
  }
}

const Strings = pickLanguage();

export default Strings;
