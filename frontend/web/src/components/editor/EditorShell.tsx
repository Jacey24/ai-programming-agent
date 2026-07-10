import { FileCode2, FileDiff, GitCompareArrows, ListChecks, ScrollText, TerminalSquare } from "lucide-react";

import type { EditorTab, EditorTabId } from "../../types";
import type {
  ChatMessageRecord,
  FileChangeRecord,
  ListState,
  LogRecord,
  ReplayPayload,
  TaskRecord,
  ToolCallRecord,
} from "../../types/api";

type ReplayItem = NonNullable<ReplayPayload["timeline"]>[number];

interface EditorShellProps {
  activeTab: EditorTabId;
  onSelectTab: (tab: EditorTabId) => void;
  task: TaskRecord | null;
  chatFallback: ChatMessageRecord | null;
  logs: ListState<LogRecord>;
  toolCalls: ListState<ToolCallRecord>;
  fileChanges: ListState<FileChangeRecord>;
  replay: ListState<ReplayItem>;
  className?: string;
}

const tabs: EditorTab[] = [
  { id: "overview", label: "Task Overview", filename: "task.readonly" },
  { id: "logs", label: "Logs", filename: "logs.txt" },
  { id: "tools", label: "Tool Calls", filename: "tool-calls.json" },
  { id: "changes", label: "File Changes", filename: "changes.diff" },
  { id: "replay", label: "Replay", filename: "replay.timeline" },
];

const tabIcon = {
  overview: FileCode2,
  logs: ScrollText,
  tools: TerminalSquare,
  changes: FileDiff,
  replay: GitCompareArrows,
};

export function EditorShell({
  activeTab,
  onSelectTab,
  task,
  chatFallback,
  logs,
  toolCalls,
  fileChanges,
  replay,
  className = "",
}: EditorShellProps) {
  const active = tabs.find((tab) => tab.id === activeTab) ?? tabs[0];
  const ActiveIcon = tabIcon[active.id];

  return (
    <section className={`min-w-0 flex-1 flex-col bg-[#0b1220] ${className || "flex"}`}>
      <div className="custom-scrollbar flex shrink-0 overflow-x-auto border-b border-slate-800 bg-black/25">
        {tabs.map((tab) => {
          const Icon = tabIcon[tab.id];
          const selected = tab.id === activeTab;
          return (
            <button
              key={tab.id}
              type="button"
              onClick={() => onSelectTab(tab.id)}
              className={`flex h-10 shrink-0 items-center gap-2 border-r border-slate-800 px-3 font-mono text-xs transition ${
                selected
                  ? "border-t-2 border-t-cyan-300 bg-[#0b1220] text-slate-100"
                  : "text-slate-500 hover:bg-white/5 hover:text-slate-200"
              }`}
            >
              <Icon className="h-3.5 w-3.5" />
              <span>{tab.filename}</span>
            </button>
          );
        })}
      </div>

      <div className="flex shrink-0 items-center gap-2 border-b border-slate-800 bg-black/15 px-4 py-2 font-mono text-[11px] text-slate-500">
        <span>workspace</span>
        <span>/</span>
        <span>codepilot</span>
        <span>/</span>
        <span className="text-slate-300">{active.label}</span>
        <span className="ml-auto rounded border border-slate-700 px-1.5 py-0.5 text-[10px] uppercase text-slate-500">
          read only
        </span>
      </div>

      <div className="custom-scrollbar min-h-0 flex-1 overflow-auto p-4">
        <div className="flex min-h-full flex-col rounded-lg border border-slate-800 bg-black/20">
          <div className="flex items-center justify-between border-b border-slate-800 px-4 py-2">
            <div className="flex items-center gap-2 font-mono text-xs font-bold text-slate-300">
              <ActiveIcon className="h-4 w-4 text-cyan-300" />
              {active.label}
            </div>
            <div className="flex items-center gap-2 font-mono text-[10px] text-slate-500">
              <ListChecks className="h-3.5 w-3.5" />
              loading / empty / error
            </div>
          </div>
          {activeTab === "overview" ? (
            <Overview task={task} chatFallback={chatFallback} />
          ) : activeTab === "logs" ? (
            <ListPanel
              state={logs}
              emptyTitle="No logs"
              renderItem={(item) => ({
                title: item.type || "log",
                meta: item.created_at || "",
                body: item.content || "",
              })}
            />
          ) : activeTab === "tools" ? (
            <ListPanel
              state={toolCalls}
              emptyTitle="No tool calls"
              renderItem={(item) => ({
                title: item.tool_name || "tool",
                meta: item.created_at || "",
                body: JSON.stringify(
                  {
                    arguments: item.arguments,
                    success: item.success,
                    exit_code: item.exit_code,
                    result: item.result,
                  },
                  null,
                  2,
                ),
                code: true,
              })}
            />
          ) : activeTab === "changes" ? (
            <ListPanel
              state={fileChanges}
              emptyTitle="No file changes"
              renderItem={(item) => ({
                title: `${item.change_type || "changed"} ${item.file_path || ""}`.trim(),
                meta: item.created_at || "",
                body: item.diff || item.file_path || "",
                code: Boolean(item.diff),
              })}
            />
          ) : (
            <ListPanel
              state={replay}
              emptyTitle="No replay timeline"
              renderItem={(item) => ({
                title: item.type || item.tool_name || "replay",
                meta: item.created_at || "",
                body: item.content || item.file_path || "",
              })}
            />
          )}
        </div>
      </div>
    </section>
  );
}

function Overview({ task, chatFallback }: { task: TaskRecord | null; chatFallback: ChatMessageRecord | null }) {
  if (chatFallback) {
    return (
      <div className="grid min-h-[420px] flex-1 place-items-center p-6">
        <div className="max-w-2xl rounded-lg border border-slate-800 bg-black/20 p-5">
          <h2 className="font-mono text-base font-bold text-slate-100">Chat fallback response</h2>
          <p className="mt-3 text-sm leading-6 text-slate-400">{chatFallback.response || "Backend returned an empty response."}</p>
          <pre className="mt-4 overflow-auto rounded border border-slate-800 bg-slate-950 p-3 font-mono text-xs text-slate-300">
            {JSON.stringify(chatFallback, null, 2)}
          </pre>
        </div>
      </div>
    );
  }

  if (!task) {
    return <EmptyState title="No active task" text="Submit a prompt or select a history task to load real backend data." />;
  }

  return (
    <div className="custom-scrollbar flex-1 overflow-auto p-5">
      <div className="grid gap-4 xl:grid-cols-[minmax(0,0.7fr)_minmax(280px,0.3fr)]">
        <section className="rounded-lg border border-slate-800 bg-black/20 p-4">
          <div className="mb-3 flex items-center justify-between gap-2">
            <h2 className="font-mono text-base font-bold text-slate-100">{task.goal || task.input || "Task"}</h2>
            <span className="rounded border border-slate-700 px-2 py-1 font-mono text-xs uppercase text-slate-300">
              {task.status || "created"}
            </span>
          </div>
          <pre className="min-h-72 whitespace-pre-wrap rounded border border-slate-800 bg-slate-950 p-4 font-mono text-xs leading-6 text-slate-300">
            {formatTaskBody(task)}
          </pre>
        </section>
        <section className="rounded-lg border border-slate-800 bg-black/20 p-4">
          <h3 className="mb-3 font-mono text-xs font-bold uppercase text-slate-300">Identity</h3>
          <dl className="space-y-2 font-mono text-xs">
            <Row label="Task" value={task.id} />
            <Row label="Session" value={task.session_id || "-"} />
            <Row label="Workspace" value={task.workspace_id || "-"} />
            <Row label="Created" value={task.created_at || "-"} />
            <Row label="Updated" value={task.updated_at || "-"} />
          </dl>
        </section>
      </div>
    </div>
  );
}

function formatTaskBody(task: TaskRecord) {
  const plan = normalizePlan(task.plan);
  const lines = [
    `Task ID: ${task.id}`,
    `Status: ${task.status || "created"}`,
    task.current_step ? `Current step: ${task.current_step}` : "",
    "",
    "Goal:",
    task.goal || task.input || "-",
    "",
    "Plan:",
    plan.length ? plan.map((item, index) => `${index + 1}. ${item}`).join("\n") : "-",
  ];

  return lines.filter((line, index) => line || lines[index - 1] !== "").join("\n");
}

function normalizePlan(plan: unknown): string[] {
  if (Array.isArray(plan)) {
    return plan.map((item) => (typeof item === "string" ? item : JSON.stringify(item)));
  }

  if (typeof plan === "string" && plan.trim()) {
    try {
      const parsed = JSON.parse(plan) as unknown;
      return Array.isArray(parsed) ? parsed.map((item) => (typeof item === "string" ? item : JSON.stringify(item))) : [plan];
    } catch {
      return [plan];
    }
  }

  return [];
}

interface ListPanelProps<T> {
  state: ListState<T>;
  emptyTitle: string;
  renderItem: (item: T) => { title: string; meta?: string; body?: string; code?: boolean };
}

function ListPanel<T>({ state, emptyTitle, renderItem }: ListPanelProps<T>) {
  if (state.status === "loading") {
    return <EmptyState title="Loading" text="Reading real backend data." />;
  }

  if (state.status === "error") {
    return <EmptyState title="Load failed" text={state.error} danger />;
  }

  if (!state.items.length) {
    return <EmptyState title={emptyTitle} text="The backend returned an empty list." />;
  }

  return (
    <div className="custom-scrollbar flex-1 space-y-3 overflow-auto p-4">
      {state.items.map((item, index) => {
        const view = renderItem(item);
        return (
          <article key={index} className="rounded-lg border border-slate-800 bg-black/25 p-4">
            <div className="mb-2 flex items-center justify-between gap-3">
              <h3 className="truncate font-mono text-sm font-bold text-slate-200">{view.title}</h3>
              <span className="shrink-0 font-mono text-[10px] text-slate-500">{view.meta}</span>
            </div>
            {view.code ? (
              <pre className="overflow-auto whitespace-pre-wrap rounded border border-slate-800 bg-slate-950 p-3 font-mono text-xs leading-5 text-slate-300">
                {view.body || "-"}
              </pre>
            ) : (
              <p className="whitespace-pre-wrap text-sm leading-6 text-slate-400">{view.body || "-"}</p>
            )}
          </article>
        );
      })}
    </div>
  );
}

function EmptyState({ title, text, danger = false }: { title: string; text: string; danger?: boolean }) {
  return (
    <div className="grid min-h-[420px] flex-1 place-items-center p-6">
      <div className="max-w-xl text-center">
        <FileCode2 className={`mx-auto mb-4 h-14 w-14 ${danger ? "text-rose-400" : "text-slate-700"}`} />
        <h2 className="font-mono text-base font-bold text-slate-200">{title}</h2>
        <p className={`mt-3 text-sm leading-6 ${danger ? "text-rose-300" : "text-slate-500"}`}>{text}</p>
      </div>
    </div>
  );
}

function Row({ label, value }: { label: string; value: string }) {
  return (
    <div className="grid grid-cols-[86px_minmax(0,1fr)] gap-2">
      <dt className="text-slate-600">{label}</dt>
      <dd className="truncate text-slate-300">{value}</dd>
    </div>
  );
}
