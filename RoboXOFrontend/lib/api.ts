import { createApi, fetchBaseQuery } from "@reduxjs/toolkit/query/react";
import {
  DEFAULT_ROBOT_STATE,
  wsMessageToRobotState,
  type RobotState,
  type WsMessage,
} from "@/types";

const API_BASE = process.env.NEXT_PUBLIC_API_BASE ?? "";

function getWsUrl(): string {
  if (typeof window === "undefined") return "";

  // Absolute URL with protocol — replace http(s) with ws(s)
  if (/^https?:\/\//.test(API_BASE)) {
    return API_BASE.replace(/^http/, "ws") + "/ws/state";
  }

  // Relative path or empty — derive from current origin
  const proto = window.location.protocol === "https:" ? "wss:" : "ws:";
  return `${proto}//${window.location.host}/ws/state`;
}

export interface JogParams {
  mode: "joint" | "cartesian";
  axis: number;
  direction: 1 | -1;
  speed_ratio: number;
  expires_ms?: number;
}

export interface SystemStatus {
  ipc: boolean;
  uptime_s: number;
}

export const api = createApi({
  reducerPath: "api",
  baseQuery: fetchBaseQuery({ baseUrl: API_BASE }),
  endpoints: (builder) => ({
    robotState: builder.query<RobotState, void>({
      queryFn: () => ({ data: { ...DEFAULT_ROBOT_STATE } }),
      async onCacheEntryAdded(
        _,
        { updateCachedData, cacheDataLoaded, cacheEntryRemoved }
      ) {
        await cacheDataLoaded;

        let ws: WebSocket | null = null;
        let heartbeatTimer: ReturnType<typeof setInterval> | null = null;
        let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
        let reconnectDelay = 2000;
        let stopped = false;

        function connect() {
          if (stopped) return;
          const url = getWsUrl();
          if (!url) return;

          ws = new WebSocket(url);

          ws.onopen = () => {
            reconnectDelay = 2000;
            heartbeatTimer = setInterval(() => {
              if (ws?.readyState === WebSocket.OPEN) {
                ws.send(JSON.stringify({ type: "heartbeat" }));
              }
            }, 5000);
          };

          ws.onmessage = (e) => {
            try {
              const msg: WsMessage = JSON.parse(e.data);
              updateCachedData((draft) => {
                Object.assign(draft, wsMessageToRobotState(msg));
              });
            } catch {
              // ignore malformed messages
            }
          };

          ws.onclose = () => {
            if (heartbeatTimer) {
              clearInterval(heartbeatTimer);
              heartbeatTimer = null;
            }
            if (!stopped) {
              updateCachedData((draft) => {
                draft.safetyState = "disconnected";
              });
              reconnectTimer = setTimeout(() => {
                reconnectDelay = Math.min(reconnectDelay * 2, 10000);
                connect();
              }, reconnectDelay);
            }
          };

          ws.onerror = () => {
            ws?.close();
          };
        }

        connect();

        await cacheEntryRemoved;
        stopped = true;
        if (heartbeatTimer) clearInterval(heartbeatTimer);
        if (reconnectTimer) clearTimeout(reconnectTimer);
        // ws?.close();
      },
    }),

    jog: builder.mutation<void, JogParams>({
      query: (body) => ({
        url: "/api/robot/jog",
        method: "POST",
        body: { ...body, expires_ms: body.expires_ms ?? 300 },
      }),
    }),

    jogStop: builder.mutation<void, void>({
      query: () => ({ url: "/api/robot/jog/stop", method: "POST" }),
    }),

    estop: builder.mutation<void, void>({
      query: () => ({ url: "/api/robot/estop", method: "POST" }),
    }),

    ioSet: builder.mutation<void, { pin: number; value: number }>({
      query: (body) => ({
        url: "/api/robot/io",
        method: "POST",
        body,
      }),
    }),

    systemStatus: builder.query<SystemStatus, void>({
      query: () => "/api/system/status",
    }),

    gameStart: builder.mutation<void, { difficulty: string; first_player: string }>({
      query: (body) => ({
        url: "/api/game/start",
        method: "POST",
        body,
      }),
    }),

    gameProceed: builder.mutation<void, void>({
      query: () => ({ url: "/api/game/proceed", method: "POST" }),
    }),

    gameReset: builder.mutation<void, void>({
      query: () => ({ url: "/api/game/reset", method: "POST" }),
    }),
  }),
});

export const {
  useRobotStateQuery,
  useJogMutation,
  useJogStopMutation,
  useEstopMutation,
  useIoSetMutation,
  useSystemStatusQuery,
  useGameStartMutation,
  useGameProceedMutation,
  useGameResetMutation,
} = api;
