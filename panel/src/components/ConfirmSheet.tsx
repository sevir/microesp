import React from 'react';
import { Button, Text, View } from '@ray-js/ray';
import Strings from '@/strings';
import type { PendingCommand } from '@/device/model';
import styles from './ConfirmSheet.module.less';

interface Common {
  onConfirm: () => void;
  onClose: () => void;
}

/** Shut down or restart: explains the cancellable countdown. */
interface PowerProps extends Common {
  kind: PendingCommand;
  countdown: number;
  agentOnline: boolean;
}

/** User script: runs right away, there is no countdown. */
interface ScriptProps extends Common {
  kind: 'script';
  label: string;
  hostname: string;
}

type Props = PowerProps | ScriptProps;

export default function ConfirmSheet(props: Props) {
  const { onConfirm, onClose } = props;
  let title: string;
  let body: string;
  let action: string;
  let tone: string;
  if (props.kind === 'script') {
    title = Strings.confirmScriptTitle(props.label, props.hostname);
    body = Strings.confirmScriptText;
    action = Strings.runScript;
    tone = styles.dark;
  } else {
    const off = props.kind === 'off';
    title = off ? Strings.confirmOffTitle : Strings.confirmRebootTitle;
    body = Strings.confirmText(props.kind, props.countdown, props.agentOnline);
    action = off ? Strings.powerOff : Strings.reboot;
    tone = off ? styles.danger : styles.dark;
  }
  return (
    <View className={styles.backdrop}>
      <View className={styles.sheet}>
        <View className={styles.handle} />
        <View className={styles.texts}>
          <Text className={styles.title}>{title}</Text>
          <Text className={styles.body}>{body}</Text>
        </View>
        <Button className={`${styles.btn} ${tone}`} onClick={onConfirm}>
          {action}
        </Button>
        <Button className={`${styles.btn} ${styles.neutral}`} onClick={onClose}>
          {Strings.cancel}
        </Button>
      </View>
    </View>
  );
}
