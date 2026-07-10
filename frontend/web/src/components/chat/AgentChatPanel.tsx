import { Bot } from "lucide-react";

import { ChatComposer } from "./ChatComposer";
import { ChatMessageList } from "./ChatMessageList";
import type { AgentState } from "../../store/agentReducer";
import type { CreateTaskInput } from "../../types/api";

interface AgentChatPanelProps {
  state: AgentState;
  onSubmit: (input: CreateTaskInput) => void;
  onCancel: () => void;
  onResolvePermission: (permissionId: string, approved: boolean) => void;
  onOpenFile: (path: string) => void;
  className?: string;
}

export function AgentChatPanel({
  state,
  onSubmit,
  onCancel,
  onResolvePermission,
  onOpenFile,
  className = "",
}: AgentChatPanelProps) {
  const status = state.activeTask?.status || (state.chatFallback ? "chat" : "idle");
  const canCancel = Boolean(state.activeTask?.id && !["completed", "failed", "cancelled"].includes(state.activeTask.status || ""));
  const taskRunning = Boolean(state.activeTask?.id) && !["completed", "failed", "cancelled"].includes(state.activeTask?.status || "");

  return (
    <aside className={`w-full shrink-0 flex-col border-l border-slate-800 bg-[#0a101b]/95 xl:w-[410px] ${className || "hidden xl:flex"}`}>
      <div className="flex h-14 items-center justify-between gap-3 border-b border-slate-800 bg-black/25 px-4">
        <div className="flex min-w-0 items-center gap-2">
          <div className="relative grid h-8 w-8 place-items-center rounded border border-cyan-400/25 bg-cyan-400/10">
            <Bot className="h-4 w-4 text-cyan-300" />
            <span className={`absolute -right-0.5 -top-0.5 h-2 w-2 rounded-full ${state.streamStatus === "connected" ? "bg-emerald-400" : state.polling ? "bg-amber-400" : "bg-slate-600"}`} />
          </div>
          <div className="min-w-0">
            <h2 className="truncate font-mono text-sm font-bold uppercase text-slate-100">Codex Workbench</h2>
            <p className="truncate text-[11px] text-slate-500">
              {state.activeTask?.id || state.workspace?.path || "/workspace"}
            </p>
          </div>
        </div>
        <span className="rounded border border-slate-700 px-2 py-1 font-mono text-[10px] font-bold uppercase text-slate-400">
          {status}
        </span>
      </div>

      <ChatMessageList state={state} onOpenFile={onOpenFile} onResolvePermission={onResolvePermission} />
      <ChatComposer
        submitting={state.submitting}
        taskRunning={taskRunning}
        canCancel={canCancel}
        cancelling={state.cancelling}
        onSubmit={onSubmit}
        onCancel={onCancel}
      />
    </aside>
  );
}
