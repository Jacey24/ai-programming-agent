import { Braces, GitBranch, Radio } from "lucide-react";

interface StatusBarProps {
  sessionId: string;
  workspaceId: string;
  taskId: string;
  apiBase: string;
  connection: string;
}

export function StatusBar({ sessionId, workspaceId, taskId, apiBase, connection }: StatusBarProps) {
  return (
    <footer className="flex h-7 shrink-0 items-center gap-4 overflow-hidden border-t border-slate-800 bg-[#07111f] px-3 font-mono text-[11px] text-slate-400">
      <span className="flex min-w-0 items-center gap-1">
        <GitBranch className="h-3.5 w-3.5 text-cyan-300" />
        <span>Session</span>
        <strong className="truncate text-slate-200">{sessionId}</strong>
      </span>
      <span className="hidden min-w-0 items-center gap-1 sm:flex">
        <span>Workspace</span>
        <strong className="truncate text-slate-200">{workspaceId}</strong>
      </span>
      <span className="hidden min-w-0 items-center gap-1 md:flex">
        <span>Task</span>
        <strong className="truncate text-slate-200">{taskId}</strong>
      </span>
      <span className="ml-auto flex items-center gap-1">
        <Braces className="h-3.5 w-3.5 text-slate-500" />
        UTF-8
      </span>
      <span className="hidden items-center gap-1 sm:flex">
        <Radio className="h-3.5 w-3.5 text-slate-500" />
        {connection}
      </span>
      <span className="text-cyan-300">{apiBase}</span>
    </footer>
  );
}
