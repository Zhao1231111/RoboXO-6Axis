"use client";

import { usePathname, useRouter } from "next/navigation";
import {
  Bot20Regular,
  HandLeft20Regular,
  Settings20Regular,
} from "@fluentui/react-icons";
import type { ReactNode } from "react";

interface NavItem {
  path: string;
  label: string;
  icon: ReactNode;
}

const NAV_ITEMS: NavItem[] = [
  { path: "/game", label: "自动", icon: <Bot20Regular /> },
  { path: "/jog", label: "手动", icon: <HandLeft20Regular /> },
  { path: "/settings", label: "设置", icon: <Settings20Regular /> },
];

export default function Sidebar() {
  const pathname = usePathname();
  const router = useRouter();

  return (
    <nav className="flex flex-col w-20 h-full bg-sidebar shrink-0 pt-4 gap-1">
      {NAV_ITEMS.map((item) => {
        const active = pathname.startsWith(item.path);
        return (
          <button
            key={item.path}
            onClick={() => router.push(item.path)}
            className={`
              relative flex flex-col items-center justify-center gap-1
              py-3 mx-1.5 rounded-lg cursor-pointer
              text-xs font-medium transition-colors
              ${
                active
                  ? "bg-white text-foreground shadow-sm"
                  : "text-foreground/60 hover:bg-white/50 hover:text-foreground"
              }
            `}
          >
            {active && (
              <span className="absolute left-0 top-1/2 -translate-y-1/2 w-[3px] h-5 rounded-r bg-[#0F6CBD]" />
            )}
            <span className="text-lg">{item.icon}</span>
            <span>{item.label}</span>
          </button>
        );
      })}
    </nav>
  );
}
