"use client";

import { useState } from "react";
import RobotViewPlaceholder from "@/components/RobotViewPlaceholder";
import SpeedSelector from "@/components/SpeedSelector";
import JointJogPanel from "@/components/JointJogPanel";
import CartesianJogPanel from "@/components/CartesianJogPanel";
import IOControlPanel from "@/components/IOControlPanel";
import { MOCK_ROBOT_STATE } from "@/lib/mock-data";

export default function JogPage() {
  const [speed, setSpeed] = useState(0.1);

  return (
    <div className="flex h-full gap-4 p-4">
      {/* Left: 3D viewport */}
      <RobotViewPlaceholder className="w-[40%] shrink-0" />

      {/* Right: controls */}
      <div className="flex-1 flex flex-col gap-3 min-w-0">
        <SpeedSelector value={speed} onChange={setSpeed} />

        <div className="flex-1 flex gap-6 min-h-0 mt-12">
          <JointJogPanel joints={MOCK_ROBOT_STATE.joints} />
          <CartesianJogPanel pose={MOCK_ROBOT_STATE.cartesian} />
        </div>

        <IOControlPanel gripper={MOCK_ROBOT_STATE.gripper} />
      </div>
    </div>
  );
}
