"use client";

import { Button } from "@fluentui/react-components";
import { useRobotStateQuery, useSystemStatusQuery } from "@/lib/api";

function StatusDot({ ok }: { ok: boolean }) {
  return (
    <span
      className={`inline-block w-2.5 h-2.5 rounded-full ${
        ok ? "bg-[#00A651]" : "bg-[#ED1C24]"
      }`}
    />
  );
}

function Card({
  title,
  children,
}: {
  title: string;
  children: React.ReactNode;
}) {
  return (
    <div className="bg-card border border-card-border rounded-xl p-5">
      <h3 className="text-sm font-semibold text-foreground/60 mb-4">
        {title}
      </h3>
      {children}
    </div>
  );
}

function formatUptime(seconds: number): string {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  if (h > 0) return `${h}h ${m}m ${s}s`;
  if (m > 0) return `${m}m ${s}s`;
  return `${s}s`;
}

export default function SettingsPage() {
  const { data: wsState } = useRobotStateQuery();
  const { data: sysStatus } = useSystemStatusQuery(undefined, {
    pollingInterval: 5000,
  });

  const wsConnected = wsState !== undefined && wsState.safetyState !== "disconnected";
  const ipcConnected = sysStatus?.ipc ?? false;
  const uptime = sysStatus?.uptime_s ?? 0;

  return (
    <div className="h-full p-6 flex flex-col gap-5 max-w-2xl">
      <Card title="标定操作">
        <div className="flex gap-3">
          <Button appearance="primary" className="min-h-12" disabled>
            白板平面标定
          </Button>
          <Button appearance="secondary" className="min-h-12" disabled>
            手眼标定
          </Button>
        </div>
        <p className="text-xs text-foreground/40 mt-2">
          后端标定接口尚未实现
        </p>
      </Card>

      <Card title="连接状态">
        <div className="flex gap-8">
          <div className="flex items-center gap-2">
            <StatusDot ok={wsConnected} />
            <span className="text-sm">WebSocket</span>
            <span className="text-xs text-foreground/50 ml-1">
              {wsConnected ? "已连接" : "未连接"}
            </span>
          </div>
          <div className="flex items-center gap-2">
            <StatusDot ok={ipcConnected} />
            <span className="text-sm">IPC</span>
            <span className="text-xs text-foreground/50 ml-1">
              {ipcConnected ? "正常" : "未连接"}
            </span>
          </div>
        </div>
      </Card>

      <Card title="系统信息">
        <div className="flex gap-8 text-sm">
          <div>
            <span className="text-foreground/50">前端版本</span>{" "}
            <span className="font-mono">0.1.0</span>
          </div>
          <div>
            <span className="text-foreground/50">后端版本</span>{" "}
            <span className="font-mono">0.1.0</span>
          </div>
          <div>
            <span className="text-foreground/50">运行时间</span>{" "}
            <span className="font-mono">{formatUptime(uptime)}</span>
          </div>
        </div>
      </Card>
    </div>
  );
}
