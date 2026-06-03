"use client";

export default function RobotViewPlaceholder({
  className = "",
}: {
  className?: string;
}) {
  return (
    <div
      className={`relative bg-gradient-to-br from-[#E8E6E1] to-[#D5D3CE] rounded-lg overflow-hidden ${className}`}
    >
      {/* Grid overlay */}
      <svg className="absolute inset-0 w-full h-full opacity-20">
        <defs>
          <pattern
            id="grid"
            width="40"
            height="40"
            patternUnits="userSpaceOnUse"
          >
            <path
              d="M 40 0 L 0 0 0 40"
              fill="none"
              stroke="#999"
              strokeWidth="0.5"
            />
          </pattern>
        </defs>
        <rect width="100%" height="100%" fill="url(#grid)" />
      </svg>
      <div className="absolute inset-0 flex items-center justify-center">
        <span className="text-sm text-foreground/40 font-medium">
          3D 视口
        </span>
      </div>
    </div>
  );
}
