"use client";

import { Button } from "@fluentui/react-components";
import { useIoSetMutation } from "@/lib/api";
import type { GripperState } from "@/types";

const PIN_GRIPPER_OPEN = 14;
const PIN_GRIPPER_CLOSE = 15;
const PIN_ALARM = 13;

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
  const [ioSet] = useIoSetMutation();

  return (
    <div className="flex items-center gap-4 flex-wrap">
      <span className="text-sm font-semibold text-foreground/60">IO</span>
      <Button
        className="min-h-12"
        onClick={() => {
          ioSet({ pin: PIN_GRIPPER_OPEN, value: 1 });
          ioSet({ pin: PIN_GRIPPER_CLOSE, value: 0 })
        }}
      >
        夹爪开
      </Button>
      <Button
        className="min-h-12"
        onClick={() => {
          ioSet({ pin: PIN_GRIPPER_CLOSE, value: 1 });
          ioSet({ pin: PIN_GRIPPER_OPEN, value: 0 });
        }}
      >
        夹爪合
      </Button>
      <Button
        className="min-h-12"
        onClick={() => ioSet({ pin: PIN_ALARM, value: alarmActive ? 0 : 1 })}
      >
        报警
      </Button>
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
