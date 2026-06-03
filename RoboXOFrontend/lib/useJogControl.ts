"use client";

import { useCallback, useRef } from "react";
import { useJogMutation, useJogStopMutation } from "./api";

const JOG_INTERVAL_MS = 200;
const JOG_EXPIRES_MS = 300;

interface UseJogControlOptions {
  mode: "joint" | "cartesian";
  axis: number;
  direction: 1 | -1;
  speedRatio: number;
}

export function useJogControl({
  mode,
  axis,
  direction,
  speedRatio,
}: UseJogControlOptions) {
  const [jog] = useJogMutation();
  const [jogStop] = useJogStopMutation();
  const timerRef = useRef<ReturnType<typeof setInterval> | null>(null);

  const stop = useCallback(() => {
    if (timerRef.current !== null) {
      clearInterval(timerRef.current);
      timerRef.current = null;
      jogStop();
    }
  }, [jogStop]);

  const start = useCallback(() => {
    if (timerRef.current !== null) return;

    const sendJog = () => {
      jog({
        mode,
        axis,
        direction,
        speed_ratio: Math.round(speedRatio * 100),
        expires_ms: JOG_EXPIRES_MS,
      });
    };

    sendJog();
    timerRef.current = setInterval(sendJog, JOG_INTERVAL_MS);
  }, [jog, mode, axis, direction, speedRatio]);

  return {
    onPointerDown: start,
    onPointerUp: stop,
    onPointerLeave: stop,
  };
}
