"use client";

import { Button } from "@fluentui/react-components";
import type { CartesianPose } from "@/types";
import { useJogControl } from "@/lib/useJogControl";

interface CartesianJogPanelProps {
  pose: CartesianPose;
  speedRatio: number;
}

const AXES: { key: keyof CartesianPose; label: string; unit: string; axis: number }[] = [
  { key: "x", label: "X", unit: "mm", axis: 0 },
  { key: "y", label: "Y", unit: "mm", axis: 1 },
  { key: "z", label: "Z", unit: "mm", axis: 2 },
  { key: "rx", label: "Rx", unit: "°", axis: 3 },
  { key: "ry", label: "Ry", unit: "°", axis: 4 },
  { key: "rz", label: "Rz", unit: "°", axis: 5 },
];

function CartesianRow({
  label,
  value,
  unit,
  axis,
  speedRatio,
}: {
  label: string;
  value: number;
  unit: string;
  axis: number;
  speedRatio: number;
}) {
  const plus = useJogControl({
    mode: "cartesian",
    axis,
    direction: 1,
    speedRatio,
  });
  const minus = useJogControl({
    mode: "cartesian",
    axis,
    direction: -1,
    speedRatio,
  });

  return (
    <div className="flex items-center gap-2">
      <span className="w-8 text-sm font-semibold text-foreground/70 text-right">
        {label}
      </span>
      <span className="w-28 text-right font-mono text-base tabular-nums">
        {value.toFixed(1)}
        {unit}
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

export default function CartesianJogPanel({
  pose,
  speedRatio,
}: CartesianJogPanelProps) {
  return (
    <div className="flex flex-col gap-1.5">
      <p className="text-sm font-semibold text-foreground/60 mb-0.5">
        直角坐标
      </p>
      {AXES.map(({ key, label, unit, axis }) => (
        <CartesianRow
          key={key}
          label={label}
          value={pose[key]}
          unit={unit}
          axis={axis}
          speedRatio={speedRatio}
        />
      ))}
    </div>
  );
}
