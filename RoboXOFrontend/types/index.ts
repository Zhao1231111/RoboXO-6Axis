export type SafetyState =
  | "idle"
  | "task_active"
  | "countdown"
  | "estop"
  | "disconnected";

export type CellValue = null | "O" | "X";

export type BoardState = CellValue[][];

export type GripperState = "open" | "closed";

export type GamePhase =
  | "idle"
  | "alarming"
  | "grabbing_pen"
  | "waiting_human"
  | "recognizing"
  | "thinking"
  | "drawing_chess"
  | "check_end"
  | "game_over"
  | "dropping_pen"
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
  human: number;
  robot: number;
  draw: number;
}

export interface RobotState {
  joints: number[];
  cartesian: CartesianPose;
  gripper: GripperState;
  ioState: number;
  board: BoardState;
  gamePhase: GamePhase;
  safetyState: SafetyState;
  countdown: number;
  score: Score;
  gameResult: string | null;
  ipcConnected: boolean;
}

/** Wire format matching the backend WebSocket JSON message. */
export interface WsMessage {
  joints_deg: number[];
  tcp_mm_deg: number[];
  gripper: "open" | "closed";
  io_state: number;
  board: number[][];
  phase: string;
  safety: string;
  alarm_countdown: number;
  game_result: string | null;
  score: { human: number; robot: number; draw: number };
}

const BOARD_VALUE_MAP: Record<number, CellValue> = {
  0: null,
  1: "X",
  2: "O",
};

export function wsMessageToRobotState(msg: WsMessage): Partial<RobotState> {
  const tcp = msg.tcp_mm_deg;
  const cartesian: CartesianPose = {
    x: tcp[0] ?? 0,
    y: tcp[1] ?? 0,
    z: tcp[2] ?? 0,
    rx: tcp[3] ?? 0,
    ry: tcp[4] ?? 0,
    rz: tcp[5] ?? 0,
  };

  let safetyState: SafetyState;
  if (msg.alarm_countdown > 0) {
    safetyState = "countdown";
  } else {
    switch (msg.safety) {
      case "idle":
        safetyState = "idle";
        break;
      case "task_active":
        safetyState = "task_active";
        break;
      case "estop":
        safetyState = "estop";
        break;
      default:
        safetyState = "idle";
    }
  }

  const board: BoardState = msg.board.map((row) =>
    row.map((v) => BOARD_VALUE_MAP[v] ?? null)
  );

  return {
    joints: msg.joints_deg,
    cartesian,
    gripper: msg.gripper,
    ioState: msg.io_state,
    board,
    gamePhase: (msg.phase as GamePhase) || "idle",
    safetyState,
    countdown: msg.alarm_countdown,
    score: msg.score,
    gameResult: msg.game_result,
  };
}

export const DEFAULT_ROBOT_STATE: RobotState = {
  joints: [0, 0, 0, 0, 0, 0],
  cartesian: { x: 0, y: 0, z: 0, rx: 0, ry: 0, rz: 0 },
  gripper: "open",
  ioState: 0,
  board: [
    [null, null, null],
    [null, null, null],
    [null, null, null],
  ],
  gamePhase: "idle",
  safetyState: "disconnected",
  countdown: 0,
  score: { human: 0, robot: 0, draw: 0 },
  gameResult: null,
  ipcConnected: false,
};
