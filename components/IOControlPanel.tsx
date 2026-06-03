"use client";

import { Button } from "@fluentui/react-components";
import type { GripperState } from "@/types";

interface IOControlPanelProps {
  gripper: GripperState;
  alarmActive?: boolean;
}

function StatusIndicator({
  active,
  label,
}: {
  active: boolean;
  label: string;
}) {
  return (
    <span className="flex items-center gap-2 text-sm text-foreground/80">
      <span
        className={`w-3.5 h-3.5 rounded-full border-2 ${
          active
            ? "bg-[#00A651] border-[#00A651]"
            : "bg-foreground/10 border-foreground/25"
        }`}
      />
      {label}
    </span>
  );
}

export default function IOControlPanel({
  gripper,
  alarmActive = false,
}: IOControlPanelProps) {
  return (
    <div className="flex items-center gap-4 flex-wrap">
      <span className="text-sm font-semibold text-foreground/60">IO</span>
      <Button className="min-h-12">夹爪开</Button>
      <Button className="min-h-12">夹爪合</Button>
      <Button className="min-h-12">报警</Button>
      <div className="flex items-center gap-4 ml-2">
        <StatusIndicator
          active={gripper === "closed"}
          label={`夹爪: ${gripper === "closed" ? "合" : "开"}`}
        />
        <StatusIndicator
          active={alarmActive}
          label={`报警: ${alarmActive ? "开" : "关"}`}
        />
      </div>
    </div>
  );
}
