"use client";

import { useState } from "react";
import SafetyStatusBar from "@/components/SafetyStatusBar";
import Board from "@/components/Board";
import RobotViewPlaceholder from "@/components/RobotViewPlaceholder";
import GameControls from "@/components/GameControls";
import { useRobotStateQuery } from "@/lib/api";
import { MOCK_ROBOT_STATE } from "@/lib/mock-data";
import { DEFAULT_ROBOT_STATE } from "@/types";

export default function GamePage() {
  const [difficulty, setDifficulty] = useState("easy");
  const [firstMove, setFirstMove] = useState("human");
  const { data: wsState = DEFAULT_ROBOT_STATE } = useRobotStateQuery();

  // board/phase/score from mock until backend game API is implemented
  const board = MOCK_ROBOT_STATE.board;
  const gamePhase = MOCK_ROBOT_STATE.gamePhase;
  const score = MOCK_ROBOT_STATE.score;

  // safety/countdown from live WebSocket
  const safetyState = wsState.safetyState;
  const countdown = wsState.countdown;

  return (
    <div className="flex flex-col h-full">
      <SafetyStatusBar
        safetyState={safetyState}
        countdown={safetyState === "countdown" ? countdown : undefined}
      />

      <div className="flex-1 grid grid-cols-[1fr_1fr_1fr] gap-4 p-4 min-h-0">
        <div className="flex items-center justify-center">
          <Board board={board} />
        </div>

        <RobotViewPlaceholder className="h-full" />

        <GameControls
          difficulty={difficulty}
          firstMove={firstMove}
          score={score}
          gamePhase={gamePhase}
          onDifficultyChange={setDifficulty}
          onFirstMoveChange={setFirstMove}
        />
      </div>
    </div>
  );
}
