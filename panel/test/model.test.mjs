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
  canCommand,
  clampCountdown,
  formatTenths,
  mergeCodes,
  mergeDps,
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
});

test('pending command and command availability', () => {
  assert.equal(pendingCommand({ ...DEFAULT_STATE, power_off: true }), 'off');
  assert.equal(pendingCommand({ ...DEFAULT_STATE, reboot: true }), 'reboot');
  assert.equal(pendingCommand(DEFAULT_STATE), null);
  assert.equal(canCommand({ ...DEFAULT_STATE, pc_state: 'on', agent_online: true }), true);
  assert.equal(canCommand({ ...DEFAULT_STATE, pc_state: 'on_no_agent' }), false);
});

test('formatting helpers', () => {
  assert.equal(formatTenths(184), '18,4');
  assert.equal(formatTenths(1000, '.'), '100.0');
  assert.equal(clampCountdown(65), 60);
});
