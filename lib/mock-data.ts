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
  gamePhase: "moving",
  safetyState: "moving",
  countdown: 0,
  score: { wins: 2, losses: 1, draws: 0 },
  ipcConnected: true,
};
