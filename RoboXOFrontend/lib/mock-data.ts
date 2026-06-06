import type { RobotState } from "@/types";

export const MOCK_ROBOT_STATE: RobotState = {
  joints: [45.2, -30.0, 60.5, 0.0, -45.3, 12.1],
  cartesian: { x: 320.1, y: 150.3, z: 400.2, rx: 0.0, ry: 180.0, rz: 0.0 },
  gripper: "closed",
  board: [
    [null, "O", null],
    [null, "X", null],
    ["O", null, null],
  ],
  gamePhase: "idle",
  safetyState: "idle",
  countdown: 0,
  score: { human: 2, robot: 1, draw: 0 },
  gameResult: null,
  ipcConnected: true,
  ioState: 0,
};
