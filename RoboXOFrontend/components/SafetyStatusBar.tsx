"use client";

import type { SafetyState } from "@/types";
import { getSafetyConfig } from "@/lib/safety-styles";
import SafetyIcon from "./SafetyIcons";

interface SafetyStatusBarProps {
  safetyState: SafetyState;
  countdown?: number;
}

export default function SafetyStatusBar({
  safetyState,
  countdown,
}: SafetyStatusBarProps) {
  const config = getSafetyConfig(safetyState, countdown);

  return (
    <div
      className="w-full flex items-center justify-center py-3 px-4"
      style={{ backgroundColor: config.bgColor }}
    >
      <div className="bg-white shadow-lg flex items-center gap-4 px-8 py-4 w-[640px]">
        <div className="h-22 w-22 flex items-center justify-center">
            <SafetyIcon state={safetyState} />
        </div>
        <div
          className="flex-1 px-5 py-4 h-full"
          style={{ backgroundColor: config.bgColor }}
        >
          <p
            className="text-base font-bold leading-tight"
            style={{ color: config.textColor }}
          >
            {config.title}
          </p>
          <p
            className="text-2xl font-bold leading-tight mt-0.5"
            style={{ color: config.textColor }}
          >
            {config.description}
          </p>
        </div>
      </div>
    </div>
  );
}
