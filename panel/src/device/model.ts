/**
 * MicroESP thing model as seen by the panel.
 *
 * Codes and abilityIds mirror firmware/schema/dp.json (checked by
 * test/model.test.mjs). This file is plain, erasable TypeScript with no
 * imports so Node can run it directly in the tests.
 */

export const PC_STATES = ['off', 'sleep', 'booting', 'on_no_agent', 'on', 'unknown'] as const;
export const WAKE_METHODS = ['hid', 'wol', 'hid_then_wol'] as const;
export const LAST_RESULTS = [
  'ok',
  'wake_sent',
  'wake_failed',
  'cmd_rejected',
  'agent_offline',
  'cancelled',
] as const;
export const FAULT_LABELS = ['agent_lost', 'wake_failed', 'hid_not_armed', 'cloud_lost'] as const;

export type PcState = (typeof PC_STATES)[number];
export type WakeMethod = (typeof WAKE_METHODS)[number];
export type LastResult = (typeof LAST_RESULTS)[number];
export type Fault = (typeof FAULT_LABELS)[number];

export interface MicroEspState {
  pc_state: PcState;
  power_on: boolean;
  power_off: boolean;
  reboot: boolean;
  cpu_usage: number;
  mem_usage: number;
  disk_free: number;
  agent_online: boolean;
  wake_method: WakeMethod;
  pc_uptime: number;
  pc_hostname: string;
  cmd_countdown: number;
  last_result: LastResult | null;
  fault: number;
  /** Raw DP 115 value (compact JSON); parse it with parseScripts. */
  scripts: string;
  script_run: string;
}

export type Code = keyof MicroEspState;

/** Properties the panel may write (mode "rw" in dp.json). */
export type WritableCode = 'power_on' | 'power_off' | 'reboot' | 'wake_method' | 'cmd_countdown' | 'script_run';

export const ABILITY_IDS: Record<Code, number> = {
  pc_state: 101,
  power_on: 102,
  power_off: 103,
  reboot: 104,
  cpu_usage: 105,
  mem_usage: 106,
  disk_free: 107,
  agent_online: 108,
  wake_method: 109,
  pc_uptime: 110,
  pc_hostname: 111,
  cmd_countdown: 112,
  last_result: 113,
  fault: 114,
  scripts: 115,
  script_run: 116,
};

const CODE_BY_ID: Record<string, Code> = Object.fromEntries(
  Object.entries(ABILITY_IDS).map(([code, id]) => [String(id), code as Code])
);

export const COUNTDOWN_MAX = 60;
const PERCENT_MAX = 1000;
const UPTIME_MAX = 999999999;
const HOSTNAME_MAX = 64;
const SCRIPTS_MAX_LEN = 255;
const SCRIPT_ID_MAX = 12;

export const DEFAULT_STATE: MicroEspState = {
  pc_state: 'unknown',
  power_on: false,
  power_off: false,
  reboot: false,
  cpu_usage: 0,
  mem_usage: 0,
  disk_free: 0,
  agent_online: false,
  wake_method: 'hid_then_wol',
  pc_uptime: 0,
  pc_hostname: '',
  cmd_countdown: 10,
  last_result: null,
  fault: 0,
  scripts: '',
  script_run: '',
};

/** TuyaLink reports wrap values as {value, time}; DP-style sources send them bare. */
export function unwrap(raw: unknown): unknown {
  if (raw !== null && typeof raw === 'object' && 'value' in (raw as Record<string, unknown>)) {
    return (raw as Record<string, unknown>).value;
  }
  return raw;
}

function toBool(v: unknown): boolean | undefined {
  if (typeof v === 'boolean') return v;
  if (v === 'true' || v === 1) return true;
  if (v === 'false' || v === 0) return false;
  return undefined;
}

function toInt(v: unknown, min: number, max: number): number | undefined {
  const n = typeof v === 'string' ? Number(v) : v;
  if (typeof n !== 'number' || !Number.isFinite(n)) return undefined;
  return Math.min(max, Math.max(min, Math.round(n)));
}

function toEnum<T extends string>(v: unknown, range: readonly T[]): T | undefined {
  return typeof v === 'string' && (range as readonly string[]).includes(v) ? (v as T) : undefined;
}

/** Coerce one incoming value to its declared type; undefined means "ignore it". */
export function normalize(code: Code, raw: unknown): MicroEspState[Code] | undefined {
  const v = unwrap(raw);
  switch (code) {
    case 'pc_state':
      return toEnum(v, PC_STATES);
    case 'wake_method':
      return toEnum(v, WAKE_METHODS);
    case 'last_result':
      return toEnum(v, LAST_RESULTS);
    case 'power_on':
    case 'power_off':
    case 'reboot':
    case 'agent_online':
      return toBool(v);
    case 'cpu_usage':
    case 'mem_usage':
    case 'disk_free':
      return toInt(v, 0, PERCENT_MAX);
    case 'pc_uptime':
      return toInt(v, 0, UPTIME_MAX);
    case 'cmd_countdown':
      return toInt(v, 0, COUNTDOWN_MAX);
    case 'fault':
      return toInt(v, 0, (1 << FAULT_LABELS.length) - 1);
    case 'pc_hostname':
      return typeof v === 'string' ? v.slice(0, HOSTNAME_MAX) : undefined;
    case 'scripts':
      return typeof v === 'string' ? v.slice(0, SCRIPTS_MAX_LEN) : undefined;
    case 'script_run':
      return typeof v === 'string' ? v.slice(0, SCRIPT_ID_MAX) : undefined;
    default:
      return undefined;
  }
}

function isCode(k: string): k is Code {
  return Object.prototype.hasOwnProperty.call(ABILITY_IDS, k);
}

/** Merge values keyed by property code (thing model messages, dpCodes). */
export function mergeCodes(state: MicroEspState, values: Record<string, unknown> | undefined | null): MicroEspState {
  if (!values) return state;
  let next = state;
  for (const [k, raw] of Object.entries(values)) {
    if (!isCode(k)) continue;
    const v = normalize(k, raw);
    if (v === undefined || v === next[k]) continue;
    if (next === state) next = { ...state };
    (next as unknown as Record<Code, unknown>)[k] = v;
  }
  return next;
}

/** Merge values keyed by abilityId / DP id ("101".."116"). */
export function mergeDps(state: MicroEspState, dps: Record<string, unknown> | undefined | null): MicroEspState {
  if (!dps) return state;
  const byCode: Record<string, unknown> = {};
  for (const [id, raw] of Object.entries(dps)) {
    const code = CODE_BY_ID[id];
    if (code) byCode[code] = raw;
  }
  return mergeCodes(state, byCode);
}

export function activeFaults(mask: number): Fault[] {
  return FAULT_LABELS.filter((_, bit) => (mask & (1 << bit)) !== 0);
}

/**
 * Faults worth a banner. hid_not_armed is informational: the host may leave
 * remote wakeup disarmed while power on still works (as the LED does, it is not
 * an error); a wake that really fails shows up as wake_failed.
 */
export function errorFaults(mask: number): Fault[] {
  return activeFaults(mask).filter((f) => f !== 'hid_not_armed');
}

export type PendingCommand = 'off' | 'reboot';

/** A shutdown or reboot is counting down while its property reads true. */
export function pendingCommand(s: MicroEspState): PendingCommand | null {
  if (s.power_off) return 'off';
  if (s.reboot) return 'reboot';
  return null;
}

/** Power on is the main action (rather than one more option) in these states. */
export function canWake(s: MicroEspState): boolean {
  return s.pc_state === 'off' || s.pc_state === 'sleep' || s.pc_state === 'unknown';
}

export function telemetryLive(s: MicroEspState): boolean {
  return s.pc_state === 'on' && s.agent_online;
}

export function clampCountdown(n: number): number {
  return Math.min(COUNTDOWN_MAX, Math.max(0, Math.round(n)));
}

/** Telemetry comes in tenths of a percent: 184 -> "18,4" (decimal comma for "es"). */
export function formatTenths(tenths: number, decimalSep = ','): string {
  return (tenths / 10).toFixed(1).replace('.', decimalSep);
}

export function splitUptime(seconds: number): { d: number; h: number; m: number } {
  return {
    d: Math.floor(seconds / 86400),
    h: Math.floor((seconds % 86400) / 3600),
    m: Math.floor((seconds % 3600) / 60),
  };
}

/* User scripts ------------------------------------------------------------ */

export interface Script {
  id: string;
  label: string;
}

export const SCRIPTS_MAX = 5;
const SCRIPT_ID_RE = /^[a-z0-9_-]{1,12}$/;

/**
 * User scripts from DP 115: compact JSON [["<id>","<label>"],...] (cdc-v1 §3.1).
 * Accepts the JSON string or the already parsed array (bare or {value, time}).
 * Anything malformed yields no scripts; invalid or repeated entries are dropped
 * and at most SCRIPTS_MAX are kept.
 */
export function parseScripts(raw: unknown): Script[] {
  let v = unwrap(raw);
  if (typeof v === 'string') {
    if (!v.trim()) return [];
    try {
      v = JSON.parse(v);
    } catch {
      return [];
    }
  }
  if (!Array.isArray(v)) return [];
  const out: Script[] = [];
  for (const item of v) {
    if (out.length >= SCRIPTS_MAX) break;
    if (!Array.isArray(item)) continue;
    const [id, label] = item;
    if (typeof id !== 'string' || !SCRIPT_ID_RE.test(id)) continue;
    if (typeof label !== 'string' || !label.trim()) continue;
    if (out.some((s) => s.id === id)) continue;
    out.push({ id, label: label.trim() });
  }
  return out;
}

/* Cloud timers ------------------------------------------------------------ */

/** Commands a cloud timer can send. Each one writes its push-button property to true. */
export const SCHEDULE_ACTIONS = ['power_on', 'power_off', 'reboot'] as const;
export type ScheduleAction = (typeof SCHEDULE_ACTIONS)[number];

/**
 * One timer category per action: the generic timer page then edits a single
 * property, so a timer never writes two commands at once (or false, which
 * would cancel a running countdown).
 */
export const TIMER_CATEGORY: Record<ScheduleAction, string> = {
  power_on: 'mesp_power_on',
  power_off: 'mesp_power_off',
  reboot: 'mesp_reboot',
};

/** A one-shot timer only has a clock time, so it must run within the next 24 h. */
export const DELAY_MAX_MIN = 23 * 60 + 59;

function pad2(n: number): string {
  return n < 10 ? `0${n}` : String(n);
}

/** Clock time ("HH:mm", local) of a timer that runs delayMin minutes after now. */
export function timerClock(now: Date, delayMin: number): string {
  const at = new Date(Math.round((now.getTime() + delayMin * 60000) / 60000) * 60000);
  return `${pad2(at.getHours())}:${pad2(at.getMinutes())}`;
}

/** True when loops ("0000000", Sunday first) selects no weekday: the timer runs once. */
export function isOnce(loops: string): boolean {
  return !/1/.test(loops || '');
}

/** Weekday order of the day chips: Monday first, Sunday last. */
export const WEEK_ORDER = [1, 2, 3, 4, 5, 6, 0] as const;

/** Flips weekday d (0 = Sunday) in loops. */
export function toggleDay(loops: string, d: number): string {
  const days = (loops || '0000000').padEnd(7, '0').slice(0, 7).split('');
  days[d] = days[d] === '1' ? '0' : '1';
  return days.join('');
}

/** Weekdays (0 = Sunday) selected by loops. */
export function loopDays(loops: string): number[] {
  const days: number[] = [];
  for (let d = 0; d < 7; d++) if ((loops || '')[d] === '1') days.push(d);
  return days;
}

/** Minutes from now to the next run of a timer, or -1 if its time is not valid. */
export function minutesUntil(now: Date, time: string, loops: string): number {
  const m = /^(\d{1,2}):(\d{2})$/.exec(time || '');
  if (!m) return -1;
  const at = Number(m[1]) * 60 + Number(m[2]);
  const nowMin = now.getHours() * 60 + now.getMinutes();
  const once = isOnce(loops);
  for (let d = 0; d < 8; d++) {
    const diff = d * 1440 + at - nowMin;
    if (diff > 0 && (once || loops[(now.getDay() + d) % 7] === '1')) return diff;
  }
  return -1;
}

/** Action a timer runs, from its dps (keyed by abilityId or code, object or JSON). */
export function timerAction(dps: unknown): ScheduleAction | null {
  let v = dps;
  if (typeof v === 'string') {
    try {
      v = JSON.parse(v);
    } catch {
      return null;
    }
  }
  if (!v || typeof v !== 'object') return null;
  const values = v as Record<string, unknown>;
  for (const a of SCHEDULE_ACTIONS) {
    const raw = values[String(ABILITY_IDS[a])] ?? values[a];
    if (toBool(unwrap(raw)) === true) return a;
  }
  return null;
}
