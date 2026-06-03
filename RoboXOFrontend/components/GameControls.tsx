"use client";

import { Dropdown, Option, Button } from "@fluentui/react-components";
import type { Score, GamePhase } from "@/types";

interface GameControlsProps {
  difficulty: string;
  firstMove: string;
  score: Score;
  gamePhase: GamePhase;
  onDifficultyChange?: (val: string) => void;
  onFirstMoveChange?: (val: string) => void;
  onStart?: () => void;
  onStop?: () => void;
  onReset?: () => void;
}

const PHASE_LABELS: Record<GamePhase, string> = {
  idle: "就绪，等待开始",
  drawing_board: "正在绘制棋盘…",
  wait_human: "等待人类落子",
  recognizing: "正在识别棋盘…",
  robot_thinking: "机器人思考中…",
  countdown: "即将开始运动",
  moving: "机器人运动中",
  braked: "已完成，等待下一步",
  game_over: "对局结束",
  erasing: "正在擦除棋盘…",
};

const IDLE_PHASES: GamePhase[] = ["idle", "game_over"];

export default function GameControls({
  difficulty,
  firstMove,
  score,
  gamePhase,
  onDifficultyChange,
  onFirstMoveChange,
  onStart,
  onStop,
  onReset,
}: GameControlsProps) {
  const isIdle = IDLE_PHASES.includes(gamePhase);
  const isBusy =
    gamePhase === "drawing_board" ||
    gamePhase === "recognizing" ||
    gamePhase === "robot_thinking" ||
    gamePhase === "countdown" ||
    gamePhase === "moving" ||
    gamePhase === "erasing";

  return (
    <div className="flex flex-col gap-4 h-full justify-center">
      <p className="text-xl font-semibold">
        {PHASE_LABELS[gamePhase]}
      </p>

      <div className="flex flex-col gap-3">
        <label className="flex flex-col gap-1">
          <span className="text-xs font-medium text-foreground/60">难度</span>
          <Dropdown
            value={
              difficulty === "easy"
                ? "简单"
                : difficulty === "medium"
                  ? "中等"
                  : "困难"
            }
            selectedOptions={[difficulty]}
            onOptionSelect={(_, data) =>
              onDifficultyChange?.(data.optionValue as string)
            }
            disabled={!isIdle}
          >
            <Option value="easy">简单</Option>
            <Option value="medium">中等</Option>
            <Option value="hard">困难</Option>
          </Dropdown>
        </label>

        <label className="flex flex-col gap-1">
          <span className="text-xs font-medium text-foreground/60">先手</span>
          <Dropdown
            value={firstMove === "human" ? "人类先手" : "机器人先手"}
            selectedOptions={[firstMove]}
            onOptionSelect={(_, data) =>
              onFirstMoveChange?.(data.optionValue as string)
            }
            disabled={!isIdle}
          >
            <Option value="human">人类先手</Option>
            <Option value="robot">机器人先手</Option>
          </Dropdown>
        </label>
      </div>

      <div className="flex gap-2">
        {isIdle ? (
          <Button
            appearance="primary"
            className="min-h-12 flex-1"
            onClick={onStart}
          >
            开始对局
          </Button>
        ) : (
          <Button
            appearance="primary"
            className="min-h-12 flex-1 !bg-[#C4314B] !text-white hover:!bg-[#a52a3f]"
            onClick={onStop}
            disabled={isBusy}
          >
            停止对局
          </Button>
        )}
        <Button
          appearance="secondary"
          className="min-h-12 flex-1"
          onClick={onReset}
          disabled={gamePhase !== "game_over"}
        >
          重来
        </Button>
      </div>

      <div className="flex gap-4 text-sm font-mono">
        <span>
          <span className="text-foreground/50">胜</span>{" "}
          <span className="font-bold">{score.wins}</span>
        </span>
        <span>
          <span className="text-foreground/50">负</span>{" "}
          <span className="font-bold">{score.losses}</span>
        </span>
        <span>
          <span className="text-foreground/50">平</span>{" "}
          <span className="font-bold">{score.draws}</span>
        </span>
      </div>
    </div>
  );
}
