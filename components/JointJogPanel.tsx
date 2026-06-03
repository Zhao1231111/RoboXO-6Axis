"use client";

import { Button } from "@fluentui/react-components";

interface JointJogPanelProps {
  joints: number[];
}

const JOINT_LABELS = ["J1", "J2", "J3", "J4", "J5", "J6"];

export default function JointJogPanel({ joints }: JointJogPanelProps) {
  return (
    <div className="flex flex-col gap-1.5">
      <p className="text-sm font-semibold text-foreground/60 mb-0.5">
        关节坐标
      </p>
      {JOINT_LABELS.map((label, i) => (
        <div key={label} className="flex items-center gap-2">
          <span className="w-8 text-sm font-semibold text-foreground/70 text-right">
            {label}
          </span>
          <span className="w-24 text-right font-mono text-base tabular-nums">
            {joints[i]?.toFixed(1)}°
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
