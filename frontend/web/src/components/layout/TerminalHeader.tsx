import { Clock3, Database, Radio, Server, Terminal } from "lucide-react";

import { useClock } from "../../hooks/useClock";
import type { ConnectionTone, HeaderStatus } from "../../types";

interface TerminalHeaderProps {
  statuses: HeaderStatus[];
  onRefresh: () => void;
}

const toneClass: Record<ConnectionTone, string> = {
  idle: "text-slate-300 border-slate-700 bg-slate-900/70",
  ok: "text-emerald-300 border-emerald-500/30 bg-emerald-500/10",
  warning: "text-amber-300 border-amber-500/30 bg-amber-500/10",
  danger: "text-rose-300 border-rose-500/30 bg-rose-500/10",
};

const statusIcon: Record<string, typeof Server> = {
  Backend: Server,
  Database,
  SSE: Radio,
  Task: Terminal,
};

export function TerminalHeader({ statuses, onRefresh }: TerminalHeaderProps) {
  const time = useClock();

  return (
    <header className="shrink-0 border-b border-slate-800 bg-[#0b101a]/95 shadow-[0_12px_40px_rgba(0,0,0,0.32)]">
      <div className="grid min-h-20 grid-cols-[minmax(240px,320px)_minmax(0,1fr)_auto] max-xl:grid-cols-1">
        <div className="flex min-w-0 items-center gap-3 border-r border-slate-800 px-4 py-3 max-xl:border-r-0">
          <div className="grid h-10 w-10 shrink-0 place-items-center rounded-lg border border-cyan-400/35 bg-cyan-400/10">
            <Terminal className="h-5 w-5 text-cyan-300" />
          </div>
          <div className="min-w-0">
            <div className="flex min-w-0 items-center gap-2">
              <h1 className="truncate font-mono text-lg font-bold tracking-normal text-white">
                CodePilot TUI
              </h1>
              <span className="rounded border border-cyan-400/30 bg-cyan-400/10 px-1.5 py-0.5 font-mono text-[10px] font-bold uppercase text-cyan-300">
                Web
              </span>
            </div>
            <p className="mt-1 truncate text-xs text-slate-400">
              Agent IDE 控制台，React 前端骨架
            </p>
          </div>
        </div>

        <section className="status-grid grid min-w-0 border-r border-slate-800 max-xl:border-r-0">
          {statuses.map((status) => {
            const Icon = statusIcon[status.label] ?? Server;
            return (
              <article
                key={status.label}
                className="min-w-0 border-r border-slate-800/80 px-3 py-2.5 last:border-r-0 max-xl:border-t"
              >
                <div className="flex items-center gap-2 text-[11px] font-bold uppercase text-slate-500">
                  <Icon className="h-3.5 w-3.5" />
                  <span>{status.label}</span>
                </div>
                <div
                  className={`mt-1 inline-flex max-w-full items-center rounded border px-2 py-1 font-mono text-xs font-bold ${toneClass[status.tone]}`}
                >
                  <span className="truncate">{status.value}</span>
                </div>
                <p className="mt-1 truncate font-mono text-[10px] text-slate-500">
                  {status.detail}
                </p>
              </article>
            );
          })}
        </section>

        <div className="flex items-center gap-3 px-4 py-3 font-mono text-xs text-slate-400 max-xl:border-t max-xl:border-slate-800">
          <div className="flex items-center gap-2">
            <Clock3 className="h-4 w-4 text-cyan-300" />
            <span>{time}</span>
          </div>
          <button
            type="button"
            onClick={onRefresh}
            className="rounded border border-slate-700 px-2 py-1 text-[10px] font-bold uppercase text-slate-300 hover:border-cyan-400/50 hover:text-cyan-200"
          >
            refresh
          </button>
        </div>
      </div>
    </header>
  );
}
