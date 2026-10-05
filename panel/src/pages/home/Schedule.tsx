import React, { useEffect, useState } from 'react';
import { Button, Picker, Text, View, showToast, vibrateShort } from '@ray-js/ray';
import Icon from '@/components/Icon';
import Strings from '@/strings';
import { useTimers, ScheduledTimer } from '@/device/useTimers';
import {
  DELAY_MAX_MIN,
  SCHEDULE_ACTIONS,
  ScheduleAction,
  WEEK_ORDER,
  isOnce,
  loopDays,
  minutesUntil,
  timerClock,
  toggleDay,
} from '@/device/model';
import styles from './index.module.less';

const MINUTE_STEP = 5;
const ONCE = '0000000';

type Mode = 'in' | 'at';

const LABEL: Record<ScheduleAction, string> = {
  power_on: Strings.powerOn,
  power_off: Strings.powerOff,
  reboot: Strings.reboot,
};

function repeatLabel(loops: string): string {
  if (isOnce(loops)) return Strings.once;
  const days = loopDays(loops);
  if (days.length === 7) return Strings.everyDay;
  return WEEK_ORDER.filter((d) => days.includes(d))
    .map((d) => Strings.weekdays[d])
    .join(' ');
}

function leftLabel(now: Date, time: string, loops: string): string {
  const left = minutesUntil(now, time, loops);
  return left >= 0 ? Strings.inTime(Strings.formatUptime(left * 60)) : '';
}

interface Props {
  deviceId: string;
}

export default function Schedule({ deviceId }: Props) {
  const timers = useTimers(deviceId);
  const [action, setAction] = useState<ScheduleAction>('power_off');
  const [mode, setMode] = useState<Mode>('in');
  const [hours, setHours] = useState(0);
  const [minutes, setMinutes] = useState(30);
  const [clock, setClock] = useState('08:00');
  const [loops, setLoops] = useState(ONCE);
  const [busy, setBusy] = useState(false);

  // Keeps "runs at" and "in X" current while the page stays open.
  const [now, setNow] = useState(() => new Date());
  useEffect(() => {
    const t = setInterval(() => setNow(new Date()), 30000);
    return () => clearInterval(t);
  }, []);

  const delay = hours * 60 + minutes;
  const valid = mode === 'at' || (delay > 0 && delay <= DELAY_MAX_MIN);

  const stepMinutes = (dir: 1 | -1) => {
    const total = Math.min(DELAY_MAX_MIN, Math.max(0, delay + dir * MINUTE_STEP));
    setHours(Math.floor(total / 60));
    setMinutes(total % 60);
  };

  const add = () => {
    if (!valid || busy) return;
    vibrateShort({ type: 'light' });
    // "In X" is a one-shot timer at now + X: a cloud timer only has a clock time.
    const time = mode === 'in' ? timerClock(new Date(), delay) : clock;
    const repeat = mode === 'in' ? ONCE : loops;
    setBusy(true);
    timers
      .add(action, time, repeat)
      .then(() => showToast({ title: Strings.scheduled(time), icon: 'none' }))
      .catch(() => showToast({ title: Strings.scheduleFailed, icon: 'error' }))
      .then(() => setBusy(false));
  };

  const remove = (t: ScheduledTimer) => {
    vibrateShort({ type: 'light' });
    timers.remove(t).catch(() => showToast({ title: Strings.removeFailed, icon: 'error' }));
  };

  const countdownNote = action !== 'power_on' ? ` ${Strings.countdownNote}` : '';

  return (
    <View className={styles.section}>
      <View className={styles.sectionHead}>
        <Text className={styles.h2}>{Strings.schedule}</Text>
        <Text className={styles.note}>{Strings.scheduleNote}</Text>
      </View>

      <View className={styles.card}>
        <View className={`${styles.row} ${styles.rowColumn}`}>
          <Text className={styles.rowLabel}>{Strings.scheduleAction}</Text>
          <View className={styles.segment}>
            {SCHEDULE_ACTIONS.map((a) => (
              <Button
                key={a}
                className={`${styles.segBtn} ${a === action ? styles.segOn : ''}`}
                onClick={() => setAction(a)}
              >
                {LABEL[a]}
              </Button>
            ))}
          </View>
        </View>

        <View className={`${styles.row} ${styles.rowColumn} ${styles.rowLast}`}>
          <Text className={styles.rowLabel}>{Strings.when}</Text>
          <View className={styles.segment}>
            {(['in', 'at'] as Mode[]).map((m) => (
              <Button
                key={m}
                className={`${styles.segBtn} ${m === mode ? styles.segOn : ''}`}
                onClick={() => setMode(m)}
              >
                {m === 'in' ? Strings.whenIn : Strings.whenAt}
              </Button>
            ))}
          </View>

          {mode === 'in' ? (
            <View className={styles.rowColumnBody}>
              <View className={styles.delay}>
                <View className={styles.stepper}>
                  <Button className={styles.stepBtn} disabled={hours <= 0} onClick={() => setHours(hours - 1)}>
                    −
                  </Button>
                  <Text className={styles.stepValue}>{`${hours} h`}</Text>
                  <Button className={styles.stepBtn} disabled={hours >= 23} onClick={() => setHours(hours + 1)}>
                    +
                  </Button>
                </View>
                <View className={styles.stepper}>
                  <Button className={styles.stepBtn} disabled={delay <= 0} onClick={() => stepMinutes(-1)}>
                    −
                  </Button>
                  <Text className={styles.stepValue}>{`${minutes} min`}</Text>
                  <Button
                    className={styles.stepBtn}
                    disabled={delay >= DELAY_MAX_MIN}
                    onClick={() => stepMinutes(1)}
                  >
                    +
                  </Button>
                </View>
              </View>
              <Text className={styles.rowHelp}>
                {(valid ? Strings.runAt(timerClock(now, delay)) : '') + countdownNote}
              </Text>
            </View>
          ) : (
            <View className={styles.rowColumnBody}>
              <View className={styles.timeRow}>
                <Text className={styles.rowLabel}>{Strings.atTime}</Text>
                <Picker mode="time" value={clock} onChange={(e) => setClock(String(e.value))}>
                  <View className={styles.timeValue}>
                    <Text>{clock}</Text>
                  </View>
                </Picker>
              </View>
              <View className={styles.days}>
                {WEEK_ORDER.map((d) => {
                  const on = loops[d] === '1';
                  return (
                    <Button
                      key={d}
                      className={`${styles.dayBtn} ${on ? styles.dayOn : ''}`}
                      onClick={() => setLoops(toggleDay(loops, d))}
                    >
                      {Strings.weekdayInitials[d]}
                    </Button>
                  );
                })}
              </View>
              <Text className={styles.rowHelp}>
                {`${repeatLabel(loops)} · ${leftLabel(now, clock, loops)}. ${
                  isOnce(loops) ? Strings.daysHelp : ''
                }${countdownNote}`}
              </Text>
            </View>
          )}

          <Button
            className={`${styles.btn} ${styles.btnAccent} ${valid && !busy ? '' : styles.btnOff}`}
            disabled={!valid || busy}
            onClick={add}
          >
            {`${Strings.scheduleBtn} · ${LABEL[action]}`}
          </Button>
        </View>
      </View>

      <View className={styles.card}>
        <View className={`${styles.row} ${timers.list.length ? '' : styles.rowLast}`}>
          <Text className={styles.rowLabel}>{Strings.upcoming}</Text>
          {timers.list.length ? null : <Text className={styles.rowHelp}>{Strings.nothingScheduled}</Text>}
        </View>
        {timers.list.map((t, i) => {
          const left = leftLabel(now, t.time, t.loops);
          return (
            <View key={t.id} className={`${styles.row} ${i === timers.list.length - 1 ? styles.rowLast : ''}`}>
              <View className={styles.rowText}>
                <Text className={styles.rowLabel}>{`${LABEL[t.action]} · ${t.time}`}</Text>
                <Text className={styles.rowHelp}>{repeatLabel(t.loops) + (left ? ` · ${left}` : '')}</Text>
              </View>
              <Button className={styles.removeBtn} onClick={() => remove(t)}>
                <Icon name="close" color="#B42318" size={18} />
              </Button>
            </View>
          );
        })}
      </View>
    </View>
  );
}
