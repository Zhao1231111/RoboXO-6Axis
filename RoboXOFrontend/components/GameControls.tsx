"use client";

import { useEffect, useRef } from "react";
import { Dropdown, Option, Button } from "@fluentui/react-components";
import type { Score, GamePhase } from "@/types";

interface GameControlsProps {
  difficulty: string;
  firstMove: string;
  score: Score;
  gamePhase: GamePhase;
  countdown?: number;
  gameResult?: string | null;
  onDifficultyChange?: (val: string) => void;
  onFirstMoveChange?: (val: string) => void;
  onStart?: () => void;
  onProceed?: () => void;
  onReset?: () => void;
}

const PHASE_LABELS: Record<GamePhase, string> = {
  idle: "准备好",
  alarming: "警告！机器人即将起动",
  grabbing_pen: "正在抓取笔及绘制棋盘…",
  waiting_human: "请落子；请在离开机器人工作范围后按继续按钮",
  recognizing: "等待视觉识别反馈…",
  thinking: "机器人思考中…",
  drawing_chess: "机器人正在绘制落子…",
  check_end: "检查结果…",
  game_over: "对局完成",
  dropping_pen: "正在放回马克笔…",
  erasing: "正在擦除棋盘…",
};

const PROCEED_PHASES: GamePhase[] = ["waiting_human", "game_over"];

export default function GameControls({
  difficulty,
  firstMove,
  score,
  gamePhase,
  countdown,
  gameResult,
  onDifficultyChange,
  onFirstMoveChange,
  onStart,
  onProceed,
  onReset,
}: GameControlsProps) {
  const isIdle = gamePhase === "idle";
  const canProceed = PROCEED_PHASES.includes(gamePhase);

  const audioRef = useRef<HTMLAudioElement | null>(null);

  useEffect(() => {
    if (!audioRef.current) {
      audioRef.current = new Audio("/alarm.ogg");
      audioRef.current.loop = true;
    }
    const audio = audioRef.current;

    if (gamePhase === "alarming") {
      audio.currentTime = 0;
      audio.play().catch(() => {});
    } else {
      audio.pause();
      audio.currentTime = 0;
    }

    return () => {
      audio.pause();
    };
  }, [gamePhase]);

  const resultLabel =
    gameResult === "human_win"
      ? "你赢了！"
      : gameResult === "robot_win"
        ? "机器人赢了"
        : gameResult === "draw"
          ? "平局"
          : null;

  return (
    <div className="flex flex-col gap-4 h-full justify-center">
      <p className="text-3xl font-bold">
        {PHASE_LABELS[gamePhase]}
      </p>

      {gamePhase === "alarming" && countdown != null && countdown > 0 && (
        <p className="text-5xl font-mono font-bold tabular-nums text-amber-500">
          {countdown.toFixed(1)}s
        </p>
      )}

      {resultLabel && (
        <p className="text-2xl font-semibold">{resultLabel}</p>
      )}

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
            className="min-h-12 flex-1"
            onClick={onProceed}
            disabled={!canProceed}
          >
            继续
          </Button>
        )}
        <Button
          appearance="secondary"
          className="min-h-12 flex-1"
          onClick={onReset}
          disabled={isIdle}
        >
          重置
        </Button>
      </div>

      <div className="flex gap-4 text-sm font-mono">
        <span>
          <span className="text-foreground/50">胜</span>{" "}
          <span className="font-bold">{score.human}</span>
        </span>
        <span>
          <span className="text-foreground/50">负</span>{" "}
          <span className="font-bold">{score.robot}</span>
        </span>
        <span>
          <span className="text-foreground/50">平</span>{" "}
          <span className="font-bold">{score.draw}</span>
        </span>
      </div>
    </div>
  );
}
