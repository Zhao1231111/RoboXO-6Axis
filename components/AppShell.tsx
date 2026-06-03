"use client";

import { FluentProvider, webLightTheme } from "@fluentui/react-components";
import Providers from "./Providers";
import Sidebar from "./Sidebar";
import type { ReactNode } from "react";

export default function AppShell({ children }: { children: ReactNode }) {
  return (
    <Providers>
      <FluentProvider theme={webLightTheme} className="h-full">
        <div className="flex h-screen bg-background">
          <Sidebar />
          <main className="flex-1 overflow-auto">{children}</main>
        </div>
      </FluentProvider>
    </Providers>
  );
}
