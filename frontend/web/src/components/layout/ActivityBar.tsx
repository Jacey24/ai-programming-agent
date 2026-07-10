import { Files, History, ShieldAlert } from "lucide-react";

import type { ActivityItem, ActivityView } from "../../types";

interface ActivityBarProps {
  activeView: ActivityView;
  onChange: (view: ActivityView) => void;
}

const activityItems: ActivityItem[] = [
  { id: "explorer", label: "Explorer", icon: Files },
  { id: "history", label: "History", icon: History },
  { id: "permissions", label: "Permissions", icon: ShieldAlert },
];

export function ActivityBar({ activeView, onChange }: ActivityBarProps) {
  return (
    <nav className="flex w-13 shrink-0 flex-col items-center border-r border-slate-800 bg-black/35 py-3">
      <div className="flex flex-1 flex-col items-center gap-2">
        {activityItems.map((item) => {
          const Icon = item.icon;
          const active = item.id === activeView;
          return (
            <button
              key={item.id}
              type="button"
              title={item.label}
              aria-label={item.label}
              aria-current={active ? "page" : undefined}
              onClick={() => onChange(item.id)}
              className={`relative grid h-10 w-full place-items-center border-l-2 transition ${
                active
                  ? "border-cyan-300 bg-cyan-400/10 text-cyan-200"
                  : "border-transparent text-slate-500 hover:bg-white/5 hover:text-slate-200"
              }`}
            >
              <Icon className="h-5 w-5" />
            </button>
          );
        })}
      </div>
    </nav>
  );
}
