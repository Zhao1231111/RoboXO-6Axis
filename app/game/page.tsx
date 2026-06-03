"use client";

import { useState } from "react";
import SafetyStatusBar from "@/components/SafetyStatusBar";
import Board from "@/components/Board";
import RobotViewPlaceholder from "@/components/RobotViewPlaceholder";
import GameControls from "@/components/GameControls";
import { MOCK_ROBOT_STATE } from "@/lib/mock-data";
import type { SafetyState, GamePhase } from "@/types";

const SAFETY_STATES: SafetyState[] = [
  "braked",
  "countdown",
  "moving",
  "disconnected",
];

const GAME_PHASES: GamePhase[] = [
  "idle",
  "drawing_board",
  "wait_human",
  "recognizing",
  "robot_thinking",
  "countdown",
  "moving",
  "braked",
  "game_over",
  "erasing",
];

function deriveSafetyState(phase: GamePhase): SafetyState {
  switch (phase) {
    case "countdown":
      return "countdown";
    case "moving":
    case "drawing_board":
    case "erasing":
      return "moving";
    default:
      return "braked";
  }
}

export default function GamePage() {
  const [gamePhaseIndex, setGamePhaseIndex] = useState(
    GAME_PHASES.indexOf(MOCK_ROBOT_STATE.gamePhase)
  );
  const [difficulty, setDifficulty] = useState("easy");
  const [firstMove, setFirstMove] = useState("human");

  const gamePhase = GAME_PHASES[gamePhaseIndex];
  const safetyState = deriveSafetyState(gamePhase);

  return (
    <div className="flex flex-col h-full">
      <SafetyStatusBar
        safetyState={safetyState}
        countdown={safetyState === "countdown" ? 3 : undefined}
      />

      <div className="flex-1 grid grid-cols-[1fr_1fr_1fr] gap-4 p-4 min-h-0">
        <div className="flex items-center justify-center">
          <Board board={MOCK_ROBOT_STATE.board} />
        </div>

        <RobotViewPlaceholder className="h-full" />

        <GameControls
          difficulty={difficulty}
          firstMove={firstMove}
          score={MOCK_ROBOT_STATE.score}
          gamePhase={gamePhase}
          onDifficultyChange={setDifficulty}
          onFirstMoveChange={setFirstMove}
        />
      </div>

      {/* Mockup: game phase switcher */}
      <div className="flex items-center gap-1.5 px-4 pb-2 flex-wrap">
        <span className="text-[10px] text-foreground/40">Demo phase:</span>
        {GAME_PHASES.map((p, i) => (
          <button
            key={p}
            onClick={() => setGamePhaseIndex(i)}
            className={`text-[10px] px-1.5 py-0.5 rounded cursor-pointer ${
              i === gamePhaseIndex
                ? "bg-foreground/10 font-bold"
                : "text-foreground/40 hover:text-foreground/60"
            }`}
          >
            {p}
          </button>
        ))}
      </div>
    </div>
  );
}
