// Run with `npm test` (Node >= 22.6 strips the TypeScript types of model.ts).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import {
  ABILITY_IDS,
  DEFAULT_STATE,
  FAULT_LABELS,
  LAST_RESULTS,
  PC_STATES,
  WAKE_METHODS,
  activeFaults,
  canWake,
  clampCountdown,
  errorFaults,
  formatTenths,
  isOnce,
  loopDays,
  minutesUntil,
  timerAction,
  timerClock,
  toggleDay,
  mergeCodes,
  mergeDps,
  parseScripts,
  pendingCommand,
} from '../src/device/model.ts';

const schema = JSON.parse(readFileSync(new URL('../../firmware/schema/dp.json', import.meta.url), 'utf8'));
const byCode = Object.fromEntries(schema.dps.map((d) => [d.code, d]));

test('codes and abilityIds match firmware/schema/dp.json', () => {
  assert.deepEqual(
    Object.entries(ABILITY_IDS).sort(),
    schema.dps.map((d) => [d.code, d.id]).sort()
  );
});

test('enum ranges and fault labels match the schema', () => {
  assert.deepEqual([...PC_STATES], byCode.pc_state.range);
  assert.deepEqual([...WAKE_METHODS], byCode.wake_method.range);
  assert.deepEqual([...LAST_RESULTS], byCode.last_result.range);
  assert.deepEqual([...FAULT_LABELS], byCode.fault.label);
});

test('defaults match the schema', () => {
  assert.equal(DEFAULT_STATE.wake_method, byCode.wake_method.default);
  assert.equal(DEFAULT_STATE.cmd_countdown, byCode.cmd_countdown.default);
});

test('TuyaLink report values are unwrapped from {value, time}', () => {
  const s = mergeCodes(DEFAULT_STATE, {
    pc_state: { value: 'on', time: 1 },
    cpu_usage: { value: 184, time: 1 },
    agent_online: true,
  });
  assert.equal(s.pc_state, 'on');
  assert.equal(s.cpu_usage, 184);
  assert.equal(s.agent_online, true);
});

test('DPs keyed by abilityId merge like codes', () => {
  const s = mergeDps(DEFAULT_STATE, { 101: 'sleep', 109: 'wol', 111: 'thinkstation', 999: 1 });
  assert.equal(s.pc_state, 'sleep');
  assert.equal(s.wake_method, 'wol');
  assert.equal(s.pc_hostname, 'thinkstation');
});

test('invalid or unknown values are ignored', () => {
  const s = mergeCodes(DEFAULT_STATE, { pc_state: 'exploded', wake_method: 3, nope: 1 });
  assert.equal(s, DEFAULT_STATE);
});

test('numbers are clamped to their declared range', () => {
  const s = mergeCodes(DEFAULT_STATE, { cpu_usage: 4000, cmd_countdown: -3, fault: 255 });
  assert.equal(s.cpu_usage, 1000);
  assert.equal(s.cmd_countdown, 0);
  assert.equal(s.fault, 15);
});

test('fault bitmap decodes bit i to label[i]', () => {
  assert.deepEqual(activeFaults(0), []);
  assert.deepEqual(activeFaults(0b1001), ['agent_lost', 'cloud_lost']);
  assert.deepEqual(errorFaults(0b0100), []);
  assert.deepEqual(errorFaults(0b0110), ['wake_failed']);
});

test('pending command and main action', () => {
  assert.equal(pendingCommand({ ...DEFAULT_STATE, power_off: true }), 'off');
  assert.equal(pendingCommand({ ...DEFAULT_STATE, reboot: true }), 'reboot');
  assert.equal(pendingCommand(DEFAULT_STATE), null);
  assert.equal(canWake({ ...DEFAULT_STATE, pc_state: 'off' }), true);
  assert.equal(canWake({ ...DEFAULT_STATE, pc_state: 'on' }), false);
});

test('formatting helpers', () => {
  assert.equal(formatTenths(184), '18,4');
  assert.equal(formatTenths(1000, '.'), '100.0');
  assert.equal(clampCountdown(65), 60);
});

test('cloud timer helpers', () => {
  // Tuesday 2026-10-06 22:40:20 local time.
  const now = new Date(2026, 9, 6, 22, 40, 20);
  assert.equal(timerClock(now, 30), '23:10');
  assert.equal(timerClock(now, 90), '00:10');
  assert.equal(timerClock(new Date(2026, 9, 6, 22, 40, 40), 1), '22:42');

  assert.equal(isOnce('0000000'), true);
  assert.equal(isOnce('0100000'), false);
  assert.deepEqual(loopDays('1000001'), [0, 6]);
  assert.equal(toggleDay('0000000', 1), '0100000');
  assert.equal(toggleDay('0100000', 1), '0000000');
  assert.equal(toggleDay('', 0), '1000000');

  assert.equal(minutesUntil(now, '23:10', '0000000'), 30);
  assert.equal(minutesUntil(now, '08:00', '0000000'), 9 * 60 + 20);
  assert.equal(minutesUntil(now, '22:40', '0000000'), 24 * 60);
  // Next Monday 08:00 from Tuesday 22:40.
  assert.equal(minutesUntil(now, '08:00', '0100000'), 5 * 1440 + 9 * 60 + 20);
  assert.equal(minutesUntil(now, 'bad', '0000000'), -1);

  assert.equal(timerAction({ 103: true }), 'power_off');
  assert.equal(timerAction('{"104":true}'), 'reboot');
  assert.equal(timerAction({ power_on: true }), 'power_on');
  assert.equal(timerAction({ 103: false }), null);
  assert.equal(timerAction('nope'), null);
});

test('scripts DPs merge as strings', () => {
  const raw = '[["backup","Backup NAS"]]';
  const s = mergeDps(DEFAULT_STATE, { 115: raw, 116: '' });
  assert.equal(s.scripts, raw);
  assert.equal(s.script_run, '');
  assert.equal(mergeCodes(DEFAULT_STATE, { scripts: { value: raw, time: 1 } }).scripts, raw);
  assert.equal(mergeCodes(DEFAULT_STATE, { scripts: 3 }), DEFAULT_STATE);
});

test('parseScripts reads the compact list and drops invalid entries', () => {
  assert.deepEqual(parseScripts('[["backup","Backup NAS"],["lock-1","Lock"]]'), [
    { id: 'backup', label: 'Backup NAS' },
    { id: 'lock-1', label: 'Lock' },
  ]);
  assert.deepEqual(parseScripts({ value: '[["a","A"]]', time: 1 }), [{ id: 'a', label: 'A' }]);
  assert.deepEqual(parseScripts([['a', 'A']]), [{ id: 'a', label: 'A' }]);
  assert.deepEqual(
    parseScripts([
      ['Bad', 'upper case id'],
      ['toolongid_1234', 'id too long'],
      ['', 'empty id'],
      ['ok', ''],
      ['ok', '   '],
      ['num', 5],
      'nope',
      ['ok', 'Fine'],
      ['ok', 'Duplicate'],
    ]),
    [{ id: 'ok', label: 'Fine' }]
  );
  const six = JSON.stringify(['a', 'b', 'c', 'd', 'e', 'f'].map((id) => [id, id.toUpperCase()]));
  assert.deepEqual(parseScripts(six).map((s) => s.id), ['a', 'b', 'c', 'd', 'e']);
});

test('parseScripts treats missing, empty or garbage values as no scripts', () => {
  for (const raw of [undefined, null, '', '   ', '[]', 'garbage', '{"a":1}', '[["a"', 42, {}, true]) {
    assert.deepEqual(parseScripts(raw), [], String(raw));
  }
});
