"use client";

import { ToggleButton } from "@fluentui/react-components";

const SPEED_OPTIONS = [
  { label: "1%", value: 0.01 },
  { label: "5%", value: 0.05 },
  { label: "10%", value: 0.1 },
  { label: "50%", value: 0.5 },
  { label: "100%", value: 1.0 },
];

interface SpeedSelectorProps {
  value: number;
  onChange?: (value: number) => void;
}

export default function SpeedSelector({ value, onChange }: SpeedSelectorProps) {
  return (
    <div className="flex items-center gap-3">
      <span className="text-sm font-semibold text-foreground/60 shrink-0">
        速度倍率
      </span>
      <div className="flex gap-1.5">
        {SPEED_OPTIONS.map((opt) => (
          <ToggleButton
            key={opt.value}
            checked={value === opt.value}
            onClick={() => onChange?.(opt.value)}
            className="min-w-14 min-h-12"
          >
            {opt.label}
          </ToggleButton>
        ))}
      </div>
    </div>
  );
}
