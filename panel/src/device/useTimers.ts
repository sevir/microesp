import { useCallback, useEffect, useState } from 'react';
import { addTimer, offTimerUpdate, onTimerUpdate, removeTimer, syncTimerTask } from '@ray-js/ray';
import {
  ABILITY_IDS,
  SCHEDULE_ACTIONS,
  ScheduleAction,
  TIMER_CATEGORY,
  minutesUntil,
  timerAction,
} from './model';

export interface ScheduledTimer {
  id: string;
  action: ScheduleAction;
  /** "HH:mm" in the home time zone. */
  time: string;
  /** Weekdays, Sunday first; "0000000" runs once. */
  loops: string;
}

export interface Timers {
  /** Enabled timers of the three actions, soonest first. */
  list: ScheduledTimer[];
  refresh: () => void;
  /** Adds a cloud timer at "HH:mm"; loops "0000000" runs once at the next such time. */
  add: (action: ScheduleAction, time: string, loops: string) => Promise<void>;
  remove: (t: ScheduledTimer) => Promise<void>;
}

interface RawTimer {
  timerId?: string | number;
  id?: string | number;
  time?: string;
  loops?: string;
  status?: boolean | number;
  dps?: unknown;
}

function fetchCategory(deviceId: string, action: ScheduleAction): Promise<ScheduledTimer[]> {
  return new Promise((resolve) => {
    syncTimerTask({
      deviceId,
      category: TIMER_CATEGORY[action],
      success: (res) => {
        const timers = ((res?.timers ?? []) as unknown as RawTimer[])
          .filter((t) => t.status === true || t.status === 1)
          .map((t) => ({
            id: String(t.timerId ?? t.id ?? ''),
            action: timerAction(t.dps) ?? action,
            time: t.time ?? '',
            loops: t.loops ?? '0000000',
          }))
          .filter((t) => t.id && t.time);
        resolve(timers);
      },
      fail: (e) => {
        console.warn('[microesp] syncTimerTask failed', TIMER_CATEGORY[action], e.errorMsg);
        resolve([]);
      },
    });
  });
}

/**
 * Cloud timers of the device (Tuya runs them and writes the property at the
 * set time, as an automation would; the dongle needs no timer support).
 */
export function useTimers(deviceId: string): Timers {
  const [list, setList] = useState<ScheduledTimer[]>([]);

  const refresh = useCallback(() => {
    if (!deviceId) return;
    Promise.all(SCHEDULE_ACTIONS.map((a) => fetchCategory(deviceId, a))).then((all) => {
      const now = new Date();
      const next = (t: ScheduledTimer) => {
        const m = minutesUntil(now, t.time, t.loops);
        return m < 0 ? Number.MAX_SAFE_INTEGER : m;
      };
      setList(all.flat().sort((a, b) => next(a) - next(b)));
    });
  }, [deviceId]);

  useEffect(() => {
    refresh();
    // Fired when timers change, also from Tuya's timer page.
    const onUpdate = () => refresh();
    onTimerUpdate(onUpdate);
    return () => offTimerUpdate(onUpdate);
  }, [refresh]);

  const add = useCallback(
    (action: ScheduleAction, time: string, loops: string) =>
      new Promise<void>((resolve, reject) => {
        addTimer({
          deviceId,
          category: TIMER_CATEGORY[action],
          // The typings declare nested objects; the cloud takes plain DP values.
          timer: { time, loops, dps: { [ABILITY_IDS[action]]: true } as never, isAppPush: true },
          success: () => {
            refresh();
            resolve();
          },
          fail: (e) => reject(new Error(e.errorMsg)),
        });
      }),
    [deviceId, refresh]
  );

  const remove = useCallback(
    (t: ScheduledTimer) =>
      new Promise<void>((resolve, reject) => {
        removeTimer({
          deviceId,
          timerId: t.id,
          success: () => {
            setList((l) => l.filter((x) => x.id !== t.id));
            refresh();
            resolve();
          },
          fail: (e) => reject(new Error(e.errorMsg)),
        });
      }),
    [deviceId, refresh]
  );

  return { list, refresh, add, remove };
}
