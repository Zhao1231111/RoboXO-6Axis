"use client";

import { Button } from "@fluentui/react-components";
import type { CartesianPose } from "@/types";

interface CartesianJogPanelProps {
  pose: CartesianPose;
}

const AXES: { key: keyof CartesianPose; label: string; unit: string }[] = [
  { key: "x", label: "X", unit: "mm" },
  { key: "y", label: "Y", unit: "mm" },
  { key: "z", label: "Z", unit: "mm" },
  { key: "rx", label: "Rx", unit: "°" },
  { key: "ry", label: "Ry", unit: "°" },
  { key: "rz", label: "Rz", unit: "°" },
];

export default function CartesianJogPanel({ pose }: CartesianJogPanelProps) {
  return (
    <div className="flex flex-col gap-1.5">
      <p className="text-sm font-semibold text-foreground/60 mb-0.5">
        直角坐标
      </p>
      {AXES.map(({ key, label, unit }) => (
        <div key={key} className="flex items-center gap-2">
          <span className="w-8 text-sm font-semibold text-foreground/70 text-right">
            {label}
          </span>
          <span className="w-28 text-right font-mono text-base tabular-nums">
            {pose[key].toFixed(1)}
            {unit}
          </span>
          <Button appearance="primary" className="min-w-0! w-12! h-12!">
            +
          </Button>
          <Button appearance="secondary" className="min-w-0! w-12! h-12!">
            −
          </Button>
        </div>
      ))}
    </div>
  );
}
