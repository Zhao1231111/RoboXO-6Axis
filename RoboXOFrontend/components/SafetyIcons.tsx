import type { SafetyState } from "@/types";

function SafeCheckIcon({ color }: { color: string }) {
  return (
    <svg viewBox="0 0 64 64" width={64} height={64}>
      <circle cx={32} cy={32} r={30} fill={color} />
      <polyline
        points="18,34 28,44 46,22"
        fill="none"
        stroke="#FFF"
        strokeWidth={5}
        strokeLinecap="round"
        strokeLinejoin="round"
      />
    </svg>
  );
}

function ProhibitionIcon({ color }: { color: string }) {
  return (
    <svg viewBox="0 0 64 64" width={64} height={64}>
      <circle cx={32} cy={32} r={30} fill={color} />
      <circle cx={32} cy={32} r={22} fill="#FFF" />
      {/* P004-style person silhouette */}
      <circle cx={32} cy={20} r={4} fill={color} />
      <path
        d="M32 25 L32 38 M24 30 L40 30 M26 50 L32 38 L38 50"
        stroke={color}
        strokeWidth={3}
        strokeLinecap="round"
        strokeLinejoin="round"
        fill="none"
      />
      {/* Prohibition bar */}
      <line
        x1={11}
        y1={11}
        x2={53}
        y2={53}
        stroke={color}
        strokeWidth={5}
        strokeLinecap="round"
      />
    </svg>
  );
}

function CrushWarningIcon({ color }: { color: string }) {
  return (
    <svg viewBox="0 0 64 64" width={64} height={64}>
      {/* Warning triangle */}
      <polygon
        points="32,4 60,58 4,58"
        fill={color}
        stroke="#1B1B1F"
        strokeWidth={2}
        strokeLinejoin="round"
      />
      {/* Exclamation mark */}
      <line
        x1={32}
        y1={22}
        x2={32}
        y2={40}
        stroke="#1B1B1F"
        strokeWidth={4}
        strokeLinecap="round"
      />
      <circle cx={32} cy={49} r={2.5} fill="#1B1B1F" />
    </svg>
  );
}

function DisconnectIcon({ color }: { color: string }) {
  return (
    <svg viewBox="0 0 64 64" width={64} height={64}>
      <circle cx={32} cy={32} r={30} fill={color} />
      {/* Broken link / disconnect symbol */}
      <path
        d="M22 32 L14 32 A8 8 0 0 1 14 16 L24 16"
        stroke="#FFF"
        strokeWidth={4}
        strokeLinecap="round"
        fill="none"
      />
      <path
        d="M42 32 L50 32 A8 8 0 0 0 50 48 L40 48"
        stroke="#FFF"
        strokeWidth={4}
        strokeLinecap="round"
        fill="none"
      />
      {/* Break slash */}
      <line
        x1={24}
        y1={44}
        x2={40}
        y2={20}
        stroke="#FFF"
        strokeWidth={3}
        strokeLinecap="round"
      />
    </svg>
  );
}

export default function SafetyIcon({ state }: { state: SafetyState }) {
  switch (state) {
    case "idle":
      return <img src="/icons/ISO_7010_E065.svg" alt="safe" className="h-full" />;
    case "countdown":
      return <img src="/icons/ISO_7010_P004.svg" alt="countdown" className="h-full" />;
    case "task_active":
      return <img src="/icons/ISO_7010_W019.svg" alt="task active" className="h-full" />;
    case "estop":
      return <img src="/icons/ISO_7010_P010.svg" alt="estop" className="h-full" />;
    case "disconnected":
      return <img src="/icons/ISO_7010_W018.svg" alt="disconnected" className="h-full" />;
  }
}
