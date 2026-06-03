import type { SafetyState } from "@/types";

export interface SafetyConfig {
  bgColor: string;
  textColor: string;
  title: string;
  description: string;
}

const SAFETY_CONFIGS: Record<SafetyState, SafetyConfig> = {
  braked: {
    bgColor: "#237F52",
    textColor: "#FFFFFF",
    title: "安全",
    description: "已抱闸，可以接近",
  },
  countdown: {
    bgColor: "#9B2423",
    textColor: "#FFFFFF",
    title: "警告",
    description: "离开作业区域：机械臂将起动",
  },
  moving: {
    bgColor: "#F9A900",
    textColor: "#000000",
    title: "警告",
    description: "挤压危险：机械臂在移动",
  },
  disconnected: {
    bgColor: "#D05D29",
    textColor: "#FFFFFF",
    title: "错误",
    description: "连接中断 - 请检查网络",
  },
};

export function getSafetyConfig(
  state: SafetyState,
  countdown?: number
): SafetyConfig {
  const config = SAFETY_CONFIGS[state];
  if (state === "countdown" && countdown !== undefined) {
    return {
      ...config,
      description: `离开作业区域：机械臂将起动 — ${countdown}s`,
    };
  }
  return config;
}
