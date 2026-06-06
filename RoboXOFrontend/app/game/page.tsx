"use client";

import { useState } from "react";
import SafetyStatusBar from "@/components/SafetyStatusBar";
import Board from "@/components/Board";
import RobotView3D from "@/components/RobotView3D";
import GameControls from "@/components/GameControls";
import {
  useRobotStateQuery,
  useGameStartMutation,
  useGameProceedMutation,
  useGameResetMutation,
} from "@/lib/api";
import { DEFAULT_ROBOT_STATE } from "@/types";

export default function GamePage() {
  const [difficulty, setDifficulty] = useState("hard");
  const [firstMove, setFirstMove] = useState("human");
  const { data: wsState = DEFAULT_ROBOT_STATE } = useRobotStateQuery();

  const [gameStart] = useGameStartMutation();
  const [gameProceed] = useGameProceedMutation();
  const [gameReset] = useGameResetMutation();

  const board = wsState.board;
  const gamePhase = wsState.gamePhase;
  const score = wsState.score;
  const safetyState = wsState.safetyState;
  const countdown = wsState.countdown;
  const gameResult = wsState.gameResult;

  return (
    <div className="flex flex-col h-full">
      <SafetyStatusBar
        safetyState={safetyState}
        countdown={safetyState === "countdown" ? countdown : undefined}
      />

      <div className="flex-1 grid grid-cols-[1fr_1fr_1fr] gap-4 p-4 min-h-0">
        <div className="flex items-center justify-center">
          <Board board={board} showLegend={gamePhase !== "idle"} />
        </div>

        <RobotView3D joints={wsState.joints} cartesian={wsState.cartesian} className="h-full" />

        <GameControls
          difficulty={difficulty}
          firstMove={firstMove}
          score={score}
          gamePhase={gamePhase}
          countdown={countdown}
          gameResult={gameResult}
          onDifficultyChange={setDifficulty}
          onFirstMoveChange={setFirstMove}
          onStart={() => gameStart({ difficulty, first_player: firstMove })}
          onProceed={() => gameProceed()}
          onReset={() => gameReset()}
        />
      </div>
    </div>
  );
}
