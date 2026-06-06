"use client";

import type { BoardState } from "@/types";

function CellContent({ value }: { value: null | "O" | "X" }) {
  if (value === "O") {
    return (
      <svg viewBox="0 0 100 100" className="w-3/5 h-3/5">
        <circle
          cx={50}
          cy={50}
          r={36}
          fill="none"
          stroke="#0F6CBD"
          strokeWidth={8}
          strokeLinecap="round"
        />
      </svg>
    );
  }
  if (value === "X") {
    return (
      <svg viewBox="0 0 100 100" className="w-3/5 h-3/5">
        <line
          x1={20}
          y1={20}
          x2={80}
          y2={80}
          stroke="#C4314B"
          strokeWidth={8}
          strokeLinecap="round"
        />
        <line
          x1={80}
          y1={20}
          x2={20}
          y2={80}
          stroke="#C4314B"
          strokeWidth={8}
          strokeLinecap="round"
        />
      </svg>
    );
  }
  return null;
}

interface BoardProps {
  board: BoardState;
  showLegend?: boolean;
}

export default function Board({ board, showLegend }: BoardProps) {
  return (
    <div className="flex flex-col items-center gap-3 w-full max-h-full">
      <div className="grid grid-cols-3 grid-rows-3 aspect-square w-full flex-1 min-h-0 border-2 border-foreground/20 rounded-lg overflow-hidden">
        {board.flat().map((cell, i) => {
          const row = Math.floor(i / 3);
          const col = i % 3;
          return (
            <div
              key={i}
              className={`
                flex items-center justify-center bg-card
                ${col < 2 ? "border-r-2 border-foreground/15" : ""}
                ${row < 2 ? "border-b-2 border-foreground/15" : ""}
              `}
            >
              <CellContent value={cell} />
            </div>
          );
        })}
      </div>
      {showLegend && (
        <div className="flex gap-6 text-sm items-center">
          <span className="flex items-center gap-1.5">
            <svg viewBox="0 0 100 100" className="w-5 h-5">
              <circle cx={50} cy={50} r={36} fill="none" stroke="#0F6CBD" strokeWidth={10} />
            </svg>
            <span className="text-foreground/70">机器人</span>
          </span>
          <span className="flex items-center gap-1.5">
            <svg viewBox="0 0 100 100" className="w-5 h-5">
              <line x1={20} y1={20} x2={80} y2={80} stroke="#C4314B" strokeWidth={10} strokeLinecap="round" />
              <line x1={80} y1={20} x2={20} y2={80} stroke="#C4314B" strokeWidth={10} strokeLinecap="round" />
            </svg>
            <span className="text-foreground/70">玩家</span>
          </span>
        </div>
      )}
    </div>
  );
}
