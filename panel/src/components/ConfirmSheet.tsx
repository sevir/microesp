import React from 'react';
import { Button, Text, View } from '@ray-js/ray';
import Strings from '@/strings';
import type { PendingCommand } from '@/device/model';
import styles from './ConfirmSheet.module.less';

interface Props {
  kind: PendingCommand;
  countdown: number;
  onConfirm: () => void;
  onClose: () => void;
}

export default function ConfirmSheet({ kind, countdown, onConfirm, onClose }: Props) {
  const off = kind === 'off';
  return (
    <View className={styles.backdrop}>
      <View className={styles.sheet}>
        <View className={styles.handle} />
        <View className={styles.texts}>
          <Text className={styles.title}>{off ? Strings.confirmOffTitle : Strings.confirmRebootTitle}</Text>
          <Text className={styles.body}>{Strings.confirmText(kind, countdown)}</Text>
        </View>
        <Button className={`${styles.btn} ${off ? styles.danger : styles.dark}`} onClick={onConfirm}>
          {off ? Strings.powerOff : Strings.reboot}
        </Button>
        <Button className={`${styles.btn} ${styles.neutral}`} onClick={onClose}>
          {Strings.cancel}
        </Button>
      </View>
    </View>
  );
}
