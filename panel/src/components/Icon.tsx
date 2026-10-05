import React from 'react';
import { Image } from '@ray-js/ray';

// Stroke icons as SVG data URIs: mini-program views cannot render inline <svg>.
const PATHS = {
  power: '<path d="M12 3v8"/><path d="M6.4 6.6a8 8 0 1 0 11.2 0"/>',
  reboot: '<path d="M20 12a8 8 0 1 1-2.3-5.6"/><path d="M20 4v5h-5"/>',
  alert: '<path d="M12 3 2 20h20L12 3z"/><path d="M12 10v4"/><path d="M12 17h.01"/>',
  close: '<path d="M6 6l12 12"/><path d="M18 6 6 18"/>',
  more: '<circle cx="5" cy="12" r="1"/><circle cx="12" cy="12" r="1"/><circle cx="19" cy="12" r="1"/>',
};

export type IconName = keyof typeof PATHS;

interface Props {
  name: IconName;
  color: string;
  size?: number;
  strokeWidth?: number;
}

export default function Icon({ name, color, size = 20, strokeWidth = 2 }: Props) {
  const svg =
    `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="${color}" ` +
    `stroke-width="${strokeWidth}" stroke-linecap="round" stroke-linejoin="round">${PATHS[name]}</svg>`;
  return (
    <Image
      src={`data:image/svg+xml;utf8,${encodeURIComponent(svg)}`}
      style={{ width: `${size}px`, height: `${size}px`, flex: 'none' }}
    />
  );
}
