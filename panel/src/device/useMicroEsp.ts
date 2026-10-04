import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import {
  getDeviceInfo,
  getLaunchOptionsSync,
  offDeviceOnlineStatusUpdate,
  offDpDataChange,
  offReceivedThingModelMessage,
  onDeviceOnlineStatusUpdate,
  onDpDataChange,
  onReceivedThingModelMessage,
  publishThingModelMessage,
  subscribeReceivedThingModelMessage,
  unSubscribeReceivedThingModelMessage,
} from '@ray-js/ray';
import {
  DEFAULT_STATE,
  MicroEspState,
  PendingCommand,
  WritableCode,
  mergeCodes,
  mergeDps,
  pendingCommand,
} from './model';

const THING_TYPE_PROPERTY = 0;

export interface MicroEsp {
  devId: string;
  ready: boolean;
  /** Dongle reachable from the cloud (the PC state is meaningless otherwise). */
  online: boolean;
  name: string;
  state: MicroEspState;
  pending: PendingCommand | null;
  /** Seconds left of the running shutdown/reboot countdown, estimated locally. */
  countdownLeft: number;
  /** Writes one TuyaLink property (thing/property/set). Rejects if the app could not send it. */
  set: <K extends WritableCode>(code: K, value: MicroEspState[K]) => Promise<void>;
  refresh: () => void;
}

function launchDeviceId(): string {
  try {
    const q = getLaunchOptionsSync()?.query ?? {};
    return String(q.deviceId ?? q.devId ?? '');
  } catch {
    return '';
  }
}

/**
 * Live view of the MicroESP device over the TuyaLink thing model.
 *
 * Initial values come from getDeviceInfo (dpCodes by code, dps by abilityId).
 * Updates arrive as thing-model property messages (keyed by code) and, on
 * app versions that also map TuyaLink properties to DPs, as DP changes
 * (keyed by abilityId); both are merged into one state, last write wins.
 * Writes go out as thing-model property messages.
 */
export function useMicroEsp(): MicroEsp {
  const devId = useMemo(launchDeviceId, []);
  const [state, setState] = useState<MicroEspState>(DEFAULT_STATE);
  const [online, setOnline] = useState(true);
  const [name, setName] = useState('');
  const [ready, setReady] = useState(false);

  const refresh = useCallback(() => {
    if (!devId) return;
    getDeviceInfo({
      deviceId: devId,
      success: (info) => {
        setName(info.name);
        setOnline(info.isOnline);
        setState((s) => mergeCodes(mergeDps(s, info.dps), info.dpCodes));
        setReady(true);
      },
      fail: (e) => console.warn('[microesp] getDeviceInfo failed', e.errorMsg),
    });
  }, [devId]);

  useEffect(() => {
    if (!devId) return undefined;
    refresh();

    const onThing = (msg: { type: number; payload: Record<string, unknown> }) => {
      if (msg.type !== THING_TYPE_PROPERTY) return;
      setState((s) => mergeCodes(s, msg.payload));
    };
    const onDps = (e: { deviceId: string; dps: Record<string, unknown> }) => {
      if (e.deviceId !== devId) return;
      setState((s) => mergeDps(s, e.dps));
    };
    const onOnline = (e: { deviceId: string; online: boolean }) => {
      if (e.deviceId === devId) setOnline(e.online);
    };

    onReceivedThingModelMessage(onThing);
    onDpDataChange(onDps);
    onDeviceOnlineStatusUpdate(onOnline);
    subscribeReceivedThingModelMessage({
      devId,
      fail: (e) => console.warn('[microesp] thing model subscribe failed', e.errorMsg),
    });

    return () => {
      offReceivedThingModelMessage(onThing);
      offDpDataChange(onDps);
      offDeviceOnlineStatusUpdate(onOnline);
      unSubscribeReceivedThingModelMessage({ devId });
    };
  }, [devId, refresh]);

  const set = useCallback(
    <K extends WritableCode>(code: K, value: MicroEspState[K]) =>
      new Promise<void>((resolve, reject) => {
        // Optimistic: the dongle echoes the value back in its next report.
        setState((s) => ({ ...s, [code]: value }));
        publishThingModelMessage({
          devId,
          type: THING_TYPE_PROPERTY,
          payload: { [code]: value },
          success: () => resolve(),
          fail: (e) => {
            refresh();
            reject(new Error(e.errorMsg));
          },
        });
      }),
    [devId, refresh]
  );

  // The dongle reports only whether a command is counting, not the time left:
  // start a local timer from cmd_countdown when the flag goes true.
  const pending = pendingCommand(state);
  const startedAt = useRef<number | null>(null);
  const [now, setNow] = useState(Date.now());
  useEffect(() => {
    if (!pending) {
      startedAt.current = null;
      return undefined;
    }
    startedAt.current = Date.now();
    setNow(startedAt.current);
    const t = setInterval(() => setNow(Date.now()), 500);
    return () => clearInterval(t);
  }, [pending]);
  const elapsed = startedAt.current ? Math.floor((now - startedAt.current) / 1000) : 0;
  const countdownLeft = pending ? Math.max(0, state.cmd_countdown - elapsed) : 0;

  return { devId, ready, online, name, state, pending, countdownLeft, set, refresh };
}
