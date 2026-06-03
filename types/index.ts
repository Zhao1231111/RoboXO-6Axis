export type SafetyState = "braked" | "countdown" | "moving" | "disconnected";

export type CellValue = null | "O" | "X";

export type BoardState = CellValue[][];

export type GripperState = "open" | "closed";

export type GamePhase =
  | "idle"
  | "drawing_board"
  | "wait_human"
  | "recognizing"
  | "robot_thinking"
  | "countdown"
  | "moving"
  | "braked"
  | "game_over"
  | "erasing";

export interface CartesianPose {
  x: number;
  y: number;
  z: number;
  rx: number;
  ry: number;
  rz: number;
}

export interface Score {
  wins: number;
  losses: number;
  draws: number;
}

export interface RobotState {
  joints: number[];
  cartesian: CartesianPose;
  gripper: GripperState;
  board: BoardState;
  gamePhase: GamePhase;
  safetyState: SafetyState;
  countdown: number;
  score: Score;
  ipcConnected: boolean;
}
