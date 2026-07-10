import { ArrowLeft, Calendar, Clock, ExternalLink, History, Loader2, RefreshCw, ShieldCheck } from "lucide-react";
import type { LucideIcon } from "lucide-react";

import type { ChatMessageRecord, FileChangeRecord, ListState, TaskRecord } from "../../types/api";

interface HistoryPageProps {
  history: ListState<TaskRecord | ChatMessageRecord>;
  activeTaskId: string;
  onBack: () => void;
  onRefresh: () => void;
  onSelectTask: (task: TaskRecord) => void;
}

export function HistoryPage({ history, activeTaskId, onBack, onRefresh, onSelectTask }: HistoryPageProps) {
  const tasks = history.items.filter(isTaskRecord);

  return (
    <section className="flex min-w-0 flex-1 flex-col overflow-hidden bg-[#080f1e] p-4 font-mono lg:p-6">
      <div className="mb-4 flex shrink-0 flex-col gap-4 border-b border-dashed border-slate-800 pb-4 sm:flex-row sm:items-center sm:justify-between">
        <div className="flex items-center gap-3">
          <div className="grid h-11 w-11 place-items-center rounded-lg border border-cyan-400/25 bg-cyan-400/10">
            <History className="h-5 w-5 text-cyan-300" />
          </div>
          <div>
            <h2 className="text-base font-bold uppercase tracking-wide text-white">Agent Task History</h2>
            <p className="mt-1 text-xs text-slate-500">Real records from GET /api/v1/tasks. Selecting a row reloads task details.</p>
          </div>
        </div>
        <div className="flex gap-2">
          <button
            type="button"
            onClick={onRefresh}
            className="inline-flex items-center gap-2 rounded-lg border border-slate-700 px-3 py-2 text-xs font-bold text-slate-300 hover:border-cyan-400/50 hover:text-cyan-200"
          >
            <RefreshCw className="h-3.5 w-3.5" />
            Refresh
          </button>
          <button
            type="button"
            onClick={onBack}
            className="inline-flex items-center gap-2 rounded-lg border border-cyan-400/30 bg-cyan-400/10 px-3 py-2 text-xs font-bold text-cyan-200"
          >
            <ArrowLeft className="h-3.5 w-3.5" />
            Back
          </button>
        </div>
      </div>

      <div className="flex min-h-0 flex-1 flex-col overflow-hidden rounded-lg border border-slate-800 bg-black/15">
        <div className="flex shrink-0 items-center justify-between border-b border-slate-800 bg-black/30 px-4 py-3">
          <span className="text-xs font-bold uppercase text-slate-300">Execution records</span>
          <span className="text-[10px] text-slate-600">No mock scenarios, reports, or generated dates</span>
        </div>

        {history.status === "loading" ? (
          <StateBlock icon={Loader2} title="Loading history" text="Reading the backend task list." spinning />
        ) : history.status === "error" ? (
          <StateBlock icon={History} title="History load failed" text={history.error} danger />
        ) : !tasks.length ? (
          <StateBlock icon={History} title="No task history" text="Create a real task to persist a task record." />
        ) : (
          <div className="custom-scrollbar min-h-0 flex-1 overflow-auto">
            <div className="grid min-w-[920px] grid-cols-12 gap-2 border-b border-slate-800 bg-black/60 px-4 py-2.5 text-[10px] font-bold uppercase tracking-wide text-slate-500">
              <div className="col-span-4">Task</div>
              <div className="col-span-2">Category</div>
              <div className="col-span-2">Duration</div>
              <div className="col-span-2">Created</div>
              <div className="col-span-1 text-right">Status</div>
              <div className="col-span-1 text-right">Action</div>
            </div>

            <div className="min-w-[920px] divide-y divide-slate-800/70">
              {tasks.map((task) => {
                const active = task.id === activeTaskId;
                return (
                  <div
                    key={task.id}
                    className={`grid grid-cols-12 items-center gap-2 px-4 py-3 text-xs transition ${
                      active ? "bg-cyan-400/10" : "hover:bg-white/[0.03]"
                    }`}
                  >
                    <div className="col-span-4 min-w-0">
                      <div className="truncate font-bold text-slate-100">{task.goal || task.input || task.id}</div>
                      <div className="mt-1 truncate text-[10px] text-slate-600">{task.id}</div>
                    </div>
                    <div className="col-span-2">
                      <span className="rounded border border-blue-500/25 bg-blue-500/10 px-1.5 py-0.5 text-[10px] font-bold text-blue-300">
                        {detectTaskCategory(task, [])}
                      </span>
                    </div>
                    <div className="col-span-2 flex items-center gap-1 text-[11px] text-slate-400">
                      <Clock className="h-3 w-3 text-cyan-400/70" />
                      {formatDuration(task.created_at, task.updated_at)}
                    </div>
                    <div className="col-span-2 flex items-center gap-1 text-[10px] text-slate-500">
                      <Calendar className="h-3 w-3" />
                      <span className="truncate">{task.created_at || "-"}</span>
                    </div>
                    <div className="col-span-1 text-right">
                      <span className={`inline-flex items-center gap-1 rounded border px-1.5 py-0.5 text-[10px] font-bold uppercase ${statusClass(task.status)}`}>
                        <ShieldCheck className="h-3 w-3" />
                        {task.status || "created"}
                      </span>
                    </div>
                    <div className="col-span-1 text-right">
                      <button
                        type="button"
                        onClick={() => onSelectTask(task)}
                        className="inline-flex items-center gap-1 rounded border border-slate-700 px-2 py-1 text-[10px] font-bold text-slate-300 hover:border-cyan-400/50 hover:text-cyan-200"
                        title="Load task details"
                      >
                        <ExternalLink className="h-3 w-3" />
                        View
                      </button>
                    </div>
                  </div>
                );
              })}
            </div>
          </div>
        )}
      </div>
    </section>
  );
}

function isTaskRecord(item: TaskRecord | ChatMessageRecord): item is TaskRecord {
  return typeof (item as TaskRecord).status === "string" || typeof (item as TaskRecord).session_id === "string";
}

function detectTaskCategory(task: TaskRecord, changes: FileChangeRecord[]) {
  const text = `${task.goal || task.input || ""} ${changes.map((item) => item.file_path || "").join(" ")}`.toLowerCase();
  if (text.includes(".cpp") || text.includes("cmake")) return "C++";
  if (text.includes(".tsx") || text.includes("react")) return "React";
  if (text.includes("mcp")) return "MCP";
  if (text.includes("sqlite") || text.includes("database")) return "Database";
  return "Agent";
}

function formatDuration(start?: string, end?: string) {
  if (!start || !end) {
    return "-";
  }

  const ms = new Date(end).getTime() - new Date(start).getTime();
  if (!Number.isFinite(ms) || ms < 0) {
    return "-";
  }

  const seconds = Math.round(ms / 1000);
  if (seconds < 60) {
    return `${seconds}s`;
  }
  const minutes = Math.floor(seconds / 60);
  return `${minutes}m ${seconds % 60}s`;
}

function statusClass(status?: string) {
  if (status === "completed") return "border-emerald-500/25 bg-emerald-500/10 text-emerald-300";
  if (status === "failed" || status === "cancelled") return "border-rose-500/25 bg-rose-500/10 text-rose-300";
  if (status === "running") return "border-amber-500/25 bg-amber-500/10 text-amber-300";
  return "border-slate-700 bg-slate-900/70 text-slate-400";
}

function StateBlock({
  icon: Icon,
  title,
  text,
  danger = false,
  spinning = false,
}: {
  icon: LucideIcon;
  title: string;
  text: string;
  danger?: boolean;
  spinning?: boolean;
}) {
  return (
    <div className="grid min-h-0 flex-1 place-items-center p-8 text-center">
      <div className="max-w-md">
        <Icon className={`mx-auto mb-4 h-12 w-12 ${spinning ? "animate-spin" : ""} ${danger ? "text-rose-400" : "text-slate-700"}`} />
        <h3 className="text-sm font-bold text-slate-200">{title}</h3>
        <p className={`mt-2 text-xs leading-6 ${danger ? "text-rose-300" : "text-slate-500"}`}>{text}</p>
      </div>
    </div>
  );
}
