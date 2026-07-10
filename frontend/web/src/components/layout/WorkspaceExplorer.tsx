import { ChevronDown, FileClock, FileDiff, History, ListTree, RefreshCw, ScrollText, ShieldAlert, Wrench } from "lucide-react";

import type { ActivityView, WorkspaceSection } from "../../types";
import type { ChatMessageRecord, FileChangeRecord, ListState, LogRecord, PermissionRecord, TaskRecord, ToolCallRecord } from "../../types/api";

interface WorkspaceExplorerProps {
  activeView: ActivityView;
  history: ListState<TaskRecord | ChatMessageRecord>;
  permissions: ListState<PermissionRecord>;
  logs: ListState<LogRecord>;
  toolCalls: ListState<ToolCallRecord>;
  fileChanges: ListState<FileChangeRecord>;
  activeTask: TaskRecord | null;
  activeTaskId: string;
  onRefreshHistory: () => void;
  onSelectHistoryItem: (item: TaskRecord | ChatMessageRecord) => void;
}

const sections: WorkspaceSection[] = [
  {
    id: "changes",
    title: "File Changes",
    description: "GET /tasks/{id}/file-changes",
    emptyText: "File changes from the active backend task will appear here.",
  },
  {
    id: "tools",
    title: "Tool Calls",
    description: "GET /tasks/{id}/tool-calls",
    emptyText: "No tool call records for the active task yet.",
  },
  {
    id: "logs",
    title: "Task Logs",
    description: "GET /tasks/{id}/logs",
    emptyText: "No task logs for the active task yet.",
  },
  {
    id: "result",
    title: "Task Result",
    description: "GET /tasks/{id}",
    emptyText: "Create or select a real task to inspect its result.",
  },
];

const icons = {
  changes: FileDiff,
  tools: Wrench,
  logs: ScrollText,
  result: FileClock,
};

function isTaskRecord(item: TaskRecord | ChatMessageRecord): item is TaskRecord {
  return typeof (item as TaskRecord).status === "string" || typeof (item as TaskRecord).session_id === "string";
}

export function WorkspaceExplorer({
  activeView,
  history,
  permissions,
  logs,
  toolCalls,
  fileChanges,
  activeTask,
  activeTaskId,
  onRefreshHistory,
  onSelectHistoryItem,
}: WorkspaceExplorerProps) {
  const title = activeView === "history" ? "History" : activeView === "permissions" ? "Permissions" : "Explorer";
  const subtitle =
    activeView === "settings"
      ? "Frontend config"
      : activeView === "history"
        ? "Real task history from the backend database"
        : "Real backend artifacts only";

  return (
    <aside className="hidden w-72 shrink-0 flex-col border-r border-slate-800 bg-[#0d1421]/95 font-mono text-xs lg:flex">
      <div className="border-b border-slate-800 px-4 py-3">
        <div className="flex items-center justify-between">
          <span className="font-bold uppercase tracking-wide text-slate-300">{title}</span>
          <button
            type="button"
            onClick={onRefreshHistory}
            className="rounded border border-slate-700 p-1 text-slate-500 hover:border-cyan-400/50 hover:text-cyan-200"
            title="Refresh history"
          >
            <RefreshCw className="h-3.5 w-3.5" />
          </button>
        </div>
        <p className="mt-1 text-[11px] leading-relaxed text-slate-500">{subtitle}</p>
      </div>

      <div className="custom-scrollbar min-h-0 flex-1 overflow-y-auto">
        {activeView === "history" ? (
          <HistoryList
            history={history}
            activeTaskId={activeTaskId}
            onSelectHistoryItem={onSelectHistoryItem}
          />
        ) : activeView === "permissions" ? (
          <PermissionList permissions={permissions} />
        ) : activeView === "settings" ? (
          <EmptySidePanel
            icon={ListTree}
            title="No local settings"
            text="The API base path is /api/v1 and is resolved by Vite or Nginx proxying."
          />
        ) : (
          <ArtifactSections
            logs={logs}
            toolCalls={toolCalls}
            fileChanges={fileChanges}
            activeTask={activeTask}
          />
        )}
      </div>
    </aside>
  );
}

function ArtifactSections({
  logs,
  toolCalls,
  fileChanges,
  activeTask,
}: {
  logs: ListState<LogRecord>;
  toolCalls: ListState<ToolCallRecord>;
  fileChanges: ListState<FileChangeRecord>;
  activeTask: TaskRecord | null;
}) {
  return (
    <div className="divide-y divide-slate-800/70">
      <ArtifactSection
        section={sections[0]}
        state={fileChanges}
        renderItem={(item) => item.file_path || item.change_type || "file change"}
      />
      <ArtifactSection
        section={sections[1]}
        state={toolCalls}
        renderItem={(item) => item.tool_name || item.id || "tool call"}
      />
      <ArtifactSection
        section={sections[2]}
        state={logs}
        renderItem={(item) => item.content || item.type || "task log"}
      />
      <section>
        <SectionHeader section={sections[3]} />
        <div className="px-4 py-3">
          <p className="truncate text-[10px] text-slate-500">{sections[3].description}</p>
          {activeTask ? (
            <div className="mt-2 space-y-2 rounded-lg border border-slate-800 bg-black/20 p-3 font-mono text-[11px] leading-relaxed">
              <div className="flex items-center justify-between gap-2">
                <span className="truncate text-slate-300">{activeTask.goal || activeTask.input || activeTask.id}</span>
                <span className="shrink-0 rounded border border-slate-700 px-1.5 py-0.5 text-[10px] uppercase text-slate-400">
                  {activeTask.status || "created"}
                </span>
              </div>
              <div className="truncate text-slate-600">{activeTask.id}</div>
            </div>
          ) : (
            <EmptyArtifact text={sections[3].emptyText} />
          )}
        </div>
      </section>
    </div>
  );
}

function ArtifactSection<T>({
  section,
  state,
  renderItem,
}: {
  section: WorkspaceSection;
  state: ListState<T>;
  renderItem: (item: T) => string | number | undefined;
}) {
  return (
    <section>
      <SectionHeader section={section} />
      <div className="px-4 py-3">
        <p className="truncate text-[10px] text-slate-500">{section.description}</p>
        {state.status === "loading" ? (
          <EmptyArtifact text="Loading real backend data." />
        ) : state.status === "error" ? (
          <EmptyArtifact text={state.error} danger />
        ) : state.items.length ? (
          <div className="mt-2 space-y-1.5">
            {state.items.slice(0, 8).map((item, index) => (
              <div key={index} className="truncate rounded border border-slate-800 bg-black/20 px-2 py-1.5 text-[11px] text-slate-400">
                {renderItem(item) || "-"}
              </div>
            ))}
          </div>
        ) : (
          <EmptyArtifact text={section.emptyText} />
        )}
      </div>
    </section>
  );
}

function SectionHeader({ section }: { section: WorkspaceSection }) {
  const Icon = icons[section.id as keyof typeof icons];
  return (
    <button
      type="button"
      className="flex w-full items-center gap-2 bg-black/20 px-3 py-2 text-left font-bold uppercase text-slate-300"
    >
      <ChevronDown className="h-3.5 w-3.5" />
      <Icon className="h-3.5 w-3.5 text-cyan-300" />
      <span>{section.title}</span>
    </button>
  );
}

function EmptyArtifact({ text, danger = false }: { text: string; danger?: boolean }) {
  return (
    <div className={`mt-2 rounded-lg border border-dashed p-3 text-[11px] leading-relaxed ${
      danger
        ? "border-rose-500/30 bg-rose-500/10 text-rose-200"
        : "border-slate-700/80 bg-black/20 text-slate-500"
    }`}>
      {text}
    </div>
  );
}

function PermissionList({ permissions }: { permissions: ListState<PermissionRecord> }) {
  if (permissions.status === "loading") {
    return <StateBlock icon={ShieldAlert} title="Loading permissions" text="Reading GET /api/v1/permissions/pending." />;
  }

  if (permissions.status === "error") {
    return <StateBlock icon={ShieldAlert} title="Permission load failed" text={permissions.error} tone="danger" />;
  }

  if (!permissions.items.length) {
    return <StateBlock icon={ShieldAlert} title="No pending permissions" text="The backend permission queue is empty." />;
  }

  return (
    <div className="space-y-2 p-3">
      {permissions.items.map((permission) => (
        <article key={permission.id} className="rounded-lg border border-amber-500/25 bg-amber-500/5 p-3">
          <div className="flex items-center justify-between gap-2">
            <span className="truncate text-[11px] font-bold text-slate-200">
              {permission.tool_name || permission.action || "Permission"}
            </span>
            <span className="shrink-0 rounded border border-amber-500/30 px-1.5 py-0.5 text-[10px] uppercase text-amber-300">
              {permission.risk_level || "risk"}
            </span>
          </div>
          <p className="mt-2 line-clamp-3 whitespace-pre-wrap text-[10px] leading-4 text-slate-500">
            {permission.reason || permission.id}
          </p>
        </article>
      ))}
    </div>
  );
}

interface HistoryListProps {
  history: ListState<TaskRecord | ChatMessageRecord>;
  activeTaskId: string;
  onSelectHistoryItem: (item: TaskRecord | ChatMessageRecord) => void;
}

function HistoryList({ history, activeTaskId, onSelectHistoryItem }: HistoryListProps) {
  if (history.status === "loading") {
    return <StateBlock icon={History} title="Loading history" text="Reading GET /api/v1/tasks." />;
  }

  if (history.status === "error") {
    return <StateBlock icon={History} title="History load failed" text={history.error} tone="danger" />;
  }

  if (!history.items.length) {
    return <StateBlock icon={History} title="No history yet" text="Create a task to persist a database record." />;
  }

  return (
    <div className="space-y-2 p-3">
      {history.items.map((item) => {
        const task = isTaskRecord(item) ? item : null;
        const id = task?.id || String(item.id);
        const active = Boolean(task?.id && task.id === activeTaskId);
        const titleText = task ? task.goal || task.input || id : (item as ChatMessageRecord).prompt || id;
        const statusText = task?.status || "chat";
        const metaText = task ? `task ${task.id}` : "chat fallback";
        const createdAt = task?.created_at || item.created_at || "";
        return (
          <button
            key={`${task ? "task" : "chat"}-${id}`}
            type="button"
            onClick={() => onSelectHistoryItem(item)}
            disabled={!task}
            className={`w-full rounded-lg border p-3 text-left transition ${
              active
                ? "border-cyan-400/60 bg-cyan-400/10"
                : "border-slate-800 bg-black/20 hover:border-slate-600"
            } ${!task ? "cursor-default opacity-70" : ""}`}
          >
            <div className="flex items-center justify-between gap-2">
              <span className="truncate text-[11px] font-bold text-slate-200">
                {titleText}
              </span>
              <span className="shrink-0 rounded border border-slate-700 px-1.5 py-0.5 text-[10px] text-slate-400">
                {statusText}
              </span>
            </div>
            <div className="mt-2 truncate text-[10px] text-slate-500">
              {metaText}
            </div>
            <div className="mt-1 truncate text-[10px] text-slate-600">
              {createdAt}
            </div>
          </button>
        );
      })}
    </div>
  );
}

interface EmptySidePanelProps {
  icon: typeof History;
  title: string;
  text: string;
}

function EmptySidePanel({ icon: Icon, title, text }: EmptySidePanelProps) {
  return <StateBlock icon={Icon} title={title} text={text} />;
}

function StateBlock({
  icon: Icon,
  title,
  text,
  tone = "muted",
}: EmptySidePanelProps & { tone?: "muted" | "danger" }) {
  return (
    <div className="flex h-full flex-col items-center justify-center px-5 text-center">
      <Icon className={`mb-3 h-8 w-8 ${tone === "danger" ? "text-rose-400" : "text-slate-600"}`} />
      <h2 className="text-sm font-bold text-slate-300">{title}</h2>
      <p className={`mt-2 text-[11px] leading-relaxed ${tone === "danger" ? "text-rose-300" : "text-slate-500"}`}>
        {text}
      </p>
    </div>
  );
}
