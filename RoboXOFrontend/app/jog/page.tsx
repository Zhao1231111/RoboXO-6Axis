"use client";

import { useState } from "react";
import RobotView3D from "@/components/RobotView3D";
import SpeedSelector from "@/components/SpeedSelector";
import JointJogPanel from "@/components/JointJogPanel";
import CartesianJogPanel from "@/components/CartesianJogPanel";
import IOControlPanel from "@/components/IOControlPanel";
import { useRobotStateQuery } from "@/lib/api";
import { DEFAULT_ROBOT_STATE } from "@/types";

export default function JogPage() {
  const [speed, setSpeed] = useState(0.1);
  const { data: state = DEFAULT_ROBOT_STATE } = useRobotStateQuery();

  return (
    <div className="flex h-full gap-4 p-4">
      <RobotView3D joints={state.joints} cartesian={state.cartesian} className="w-[40%] shrink-0" />

      <div className="flex-1 flex flex-col gap-3 min-w-0">
        <SpeedSelector value={speed} onChange={setSpeed} />

        <div className="flex-1 flex gap-6 min-h-0 mt-12">
          <JointJogPanel joints={state.joints} speedRatio={speed} />
          <CartesianJogPanel pose={state.cartesian} speedRatio={speed} />
        </div>

        <IOControlPanel gripper={state.gripper} />
      </div>
    </div>
  );
}
