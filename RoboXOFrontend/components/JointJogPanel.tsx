"use client";

import { Button } from "@fluentui/react-components";
import { useJogControl } from "@/lib/useJogControl";

interface JointJogPanelProps {
  joints: number[];
  speedRatio: number;
}

const JOINT_LABELS = ["J1", "J2", "J3", "J4", "J5", "J6"];

function JointRow({
  label,
  value,
  axis,
  speedRatio,
}: {
  label: string;
  value: number;
  axis: number;
  speedRatio: number;
}) {
  const plus = useJogControl({
    mode: "joint",
    axis,
    direction: 1,
    speedRatio,
  });
  const minus = useJogControl({
    mode: "joint",
    axis,
    direction: -1,
    speedRatio,
  });

  return (
    <div className="flex items-center gap-2">
      <span className="w-8 text-sm font-semibold text-foreground/70 text-right">
        {label}
      </span>
      <span className="w-24 text-right font-mono text-base tabular-nums">
        {value.toFixed(1)}°
      </span>
      <Button
        appearance="primary"
        className="min-w-0! w-12! h-12!"
        {...plus}
      >
        +
      </Button>
      <Button
        appearance="secondary"
        className="min-w-0! w-12! h-12!"
        {...minus}
      >
        −
      </Button>
    </div>
  );
}

export default function JointJogPanel({
  joints,
  speedRatio,
}: JointJogPanelProps) {
  return (
    <div className="flex flex-col gap-1.5">
      <p className="text-sm font-semibold text-foreground/60 mb-0.5">
        关节坐标
      </p>
      {JOINT_LABELS.map((label, i) => (
        <JointRow
          key={label}
          label={label}
          value={joints[i] ?? 0}
          axis={i}
          speedRatio={speedRatio}
        />
      ))}
    </div>
  );
}
