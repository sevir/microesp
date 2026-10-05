import React, { useState } from 'react';
import { Button, Text, View, openDeviceDetailPage, showToast, vibrateShort } from '@ray-js/ray';
import ConfirmSheet from '@/components/ConfirmSheet';
import Icon from '@/components/Icon';
import Strings from '@/strings';
import { useMicroEsp } from '@/device/useMicroEsp';
import {
  PendingCommand,
  WAKE_METHODS,
  WritableCode,
  MicroEspState,
  canWake,
  clampCountdown,
  errorFaults,
  formatTenths,
  telemetryLive,
} from '@/device/model';
import Schedule from './Schedule';
import styles from './index.module.less';

const DOT: Record<string, string> = {
  on: '#5FE3A1',
  on_no_agent: '#FFB067',
  booting: '#FFD66B',
  counting: '#FFD66B',
  sleep: '#B7A4FF',
  off: '#8A8F98',
  unknown: '#8A8F98',
};

const COUNTDOWN_STEP = 5;

export default function Home() {
  const dev = useMicroEsp();
  const { state, pending, online } = dev;
  const [ask, setAsk] = useState<PendingCommand | null>(null);

  const send = <K extends WritableCode>(code: K, value: MicroEspState[K]) => {
    vibrateShort({ type: 'light' });
    dev.set(code, value).catch(() => showToast({ title: Strings.sendFailed, icon: 'error' }));
  };

  const confirm = () => {
    if (!ask) return;
    send(ask === 'off' ? 'power_off' : 'reboot', true);
    setAsk(null);
  };

  const live = telemetryLive(state);
  const wake = canWake(state);

  let heroTitle = Strings.state[state.pc_state];
  let heroSub = Strings.sub[state.pc_state];
  if (pending) {
    heroTitle = pending === 'off' ? Strings.shuttingDown : Strings.rebooting;
    heroSub = Strings.countingSub(dev.countdownLeft);
  } else if (state.pc_state === 'on') {
    heroSub = Strings.uptimeSub(Strings.formatUptime(state.pc_uptime));
  } else if (state.pc_state === 'booting') {
    heroSub = Strings.bootingSub(Strings.wakeLabel[state.wake_method]);
  }
  const dot = DOT[pending ? 'counting' : state.pc_state];

  const tiles = [
    { key: 'cpu', label: Strings.cpu, v: state.cpu_usage, color: '#2B59FF' },
    { key: 'mem', label: Strings.memory, v: state.mem_usage, color: '#2B59FF' },
    { key: 'disk', label: Strings.diskFree, v: state.disk_free, color: '#1F8A5B' },
  ];

  return (
    <View className={styles.page}>
      <View className={styles.header}>
        <View className={styles.headerText}>
          <Text className={styles.h1}>{Strings.title}</Text>
          <View className={styles.meta}>
            <View className={styles.metaDot} style={{ background: online ? '#1F8A5B' : '#A3A7AF' }} />
            {state.pc_hostname ? <Text className={styles.mono}>{state.pc_hostname}</Text> : null}
            <Text>{`${state.pc_hostname ? '· ' : ''}${online ? Strings.dongleOnline : Strings.dongleOffline}`}</Text>
          </View>
        </View>
        <Button className={styles.iconBtn} onClick={() => openDeviceDetailPage({ deviceId: dev.devId })}>
          <Icon name="more" color="#15171C" />
        </Button>
      </View>

      <View className={styles.hero}>
        <View className={styles.heroTop}>
          <View className={styles.heroText}>
            <View className={styles.chip}>
              <View className={styles.chipDot} style={{ background: dot }} />
              <Text>{pending ? Strings.commandRunning : Strings.status}</Text>
            </View>
            <Text className={styles.heroTitle}>{heroTitle}</Text>
            {heroSub ? <Text className={styles.heroSub}>{heroSub}</Text> : null}
          </View>
          <View className={`${styles.ring} ${pending ? styles.ringActive : ''}`}>
            {pending ? (
              <Text className={styles.ringCount}>{dev.countdownLeft}</Text>
            ) : (
              <Icon name="power" color={dot} size={30} />
            )}
          </View>
        </View>

        {pending ? (
          <Button
            className={`${styles.btn} ${styles.btnLight}`}
            onClick={() => send(pending === 'off' ? 'power_off' : 'reboot', false)}
          >
            {Strings.cancel}
          </Button>
        ) : (
          // The detected state is only a hint (the BIOS enumerates the dongle even
          // with the PC off), so every command stays available whatever it says.
          <View className={styles.actions}>
            <Button
              className={`${styles.btn} ${wake ? styles.btnAccent : styles.btnGhost}`}
              onClick={() => send('power_on', true)}
            >
              <View className={styles.btnInner}>
                <Icon name="power" color={wake ? '#FFFFFF' : '#F4F4F1'} strokeWidth={2.2} />
                <Text>{state.pc_state === 'sleep' ? Strings.wakeUp : Strings.powerOn}</Text>
              </View>
            </Button>
            <View className={styles.actionRow}>
              <Button className={`${styles.btn} ${styles.btnLight}`} onClick={() => setAsk('off')}>
                <View className={styles.btnInner}>
                  <Icon name="power" color="#15171C" size={18} strokeWidth={2.2} />
                  <Text>{Strings.powerOff}</Text>
                </View>
              </Button>
              <Button className={`${styles.btn} ${styles.btnGhost}`} onClick={() => setAsk('reboot')}>
                <View className={styles.btnInner}>
                  <Icon name="reboot" color="#F4F4F1" size={18} strokeWidth={2.2} />
                  <Text>{Strings.reboot}</Text>
                </View>
              </Button>
            </View>
            {online && !state.agent_online ? <Text className={styles.hint}>{Strings.needsAgent}</Text> : null}
          </View>
        )}
      </View>

      {errorFaults(state.fault).map((f) => (
        <View key={f} className={styles.fault}>
          <Icon name="alert" color="#B54708" />
          <View className={styles.faultText}>
            <Text className={styles.faultTitle}>{Strings.faultTitle[f]}</Text>
            <Text className={styles.faultBody}>{Strings.faultText[f]}</Text>
          </View>
        </View>
      ))}

      <View className={styles.section}>
        <View className={styles.sectionHead}>
          <Text className={styles.h2}>{Strings.performance}</Text>
          <Text className={styles.note}>{live ? Strings.teleLive : Strings.teleIdle}</Text>
        </View>
        <View className={styles.tiles} style={{ opacity: live ? 1 : 0.55 }}>
          {tiles.map((t) => (
            <View key={t.key} className={styles.tile}>
              <Text className={styles.tileLabel}>{t.label}</Text>
              <View className={styles.tileValue}>
                <Text className={styles.tileNumber}>{live ? formatTenths(t.v, Strings.decimalSep) : '—'}</Text>
                {live ? <Text className={styles.tileUnit}>%</Text> : null}
              </View>
              <View className={styles.bar}>
                <View className={styles.barFill} style={{ width: `${live ? t.v / 10 : 0}%`, background: t.color }} />
              </View>
            </View>
          ))}
        </View>
      </View>

      <Schedule deviceId={dev.devId} />

      <View className={styles.section}>
        <Text className={`${styles.h2} ${styles.sectionPad}`}>{Strings.settings}</Text>
        <View className={styles.card}>
          <View className={`${styles.row} ${styles.rowColumn}`}>
            <View className={styles.rowText}>
              <Text className={styles.rowLabel}>{Strings.wakeMethod}</Text>
              <Text className={styles.rowHelp}>{Strings.wakeHelp[state.wake_method]}</Text>
            </View>
            <View className={styles.segment}>
              {WAKE_METHODS.map((m) => (
                <Button
                  key={m}
                  className={`${styles.segBtn} ${m === state.wake_method ? styles.segOn : ''}`}
                  disabled={!online}
                  onClick={() => m !== state.wake_method && send('wake_method', m)}
                >
                  {Strings.wakeLabel[m]}
                </Button>
              ))}
            </View>
          </View>

          <View className={styles.row}>
            <View className={styles.rowText}>
              <Text className={styles.rowLabel}>{Strings.countdown}</Text>
              <Text className={styles.rowHelp}>{Strings.countdownHelp}</Text>
            </View>
            <View className={styles.stepper}>
              <Button
                className={styles.stepBtn}
                disabled={!online || state.cmd_countdown <= 0}
                onClick={() => send('cmd_countdown', clampCountdown(state.cmd_countdown - COUNTDOWN_STEP))}
              >
                −
              </Button>
              <Text className={styles.stepValue}>{`${state.cmd_countdown} s`}</Text>
              <Button
                className={styles.stepBtn}
                disabled={!online || state.cmd_countdown >= 60}
                onClick={() => send('cmd_countdown', clampCountdown(state.cmd_countdown + COUNTDOWN_STEP))}
              >
                +
              </Button>
            </View>
          </View>

          <View className={styles.row}>
            <Text className={styles.rowLabel}>{Strings.agent}</Text>
            <View className={styles.rowValue}>
              <View className={styles.metaDot} style={{ background: state.agent_online ? '#1F8A5B' : '#A3A7AF' }} />
              <Text>{state.agent_online ? Strings.agentOnline : Strings.agentOffline}</Text>
            </View>
          </View>

          <View className={`${styles.row} ${styles.rowLast}`}>
            <Text className={styles.rowLabel}>{Strings.lastCommand}</Text>
            <Text className={styles.rowValue}>{state.last_result ? Strings.last[state.last_result] : '—'}</Text>
          </View>
        </View>
      </View>

      {ask ? (
        <ConfirmSheet
          kind={ask}
          countdown={state.cmd_countdown}
          agentOnline={state.agent_online}
          onConfirm={confirm}
          onClose={() => setAsk(null)}
        />
      ) : null}
    </View>
  );
}
