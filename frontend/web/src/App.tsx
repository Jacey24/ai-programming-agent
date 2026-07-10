import { useEffect, useMemo, useReducer, useRef } from "react";
import { ChevronDown, ChevronUp } from "lucide-react";

import { AgentChatPanel } from "./components/chat/AgentChatPanel";
import { CodeEditor } from "./components/editor/CodeEditor";
import { FileExplorer } from "./components/explorer/FileExplorer";
import { HistoryPage } from "./components/history/HistoryPage";
import { ActivityBar } from "./components/layout/ActivityBar";
import { StatusBar } from "./components/layout/StatusBar";
import { TerminalHeader } from "./components/layout/TerminalHeader";
import { useAgentRuntime } from "./hooks/useAgentRuntime";
import { useWorkspaceWorkbench } from "./hooks/useWorkspaceWorkbench";
import type { AppAction, AppState, HeaderStatus } from "./types";
import type { AgentState } from "./store/agentReducer";

const initialState: AppState = {
  activeView: "explorer",
  activeEditorTab: "overview",
};

function reducer(state: AppState, action: AppAction): AppState {
  switch (action.type) {
    case "setActiveView":
      return { ...state, activeView: action.view };
    case "setActiveEditorTab":
      return { ...state, activeEditorTab: action.tab };
    default:
      return state;
  }
}

export default function App() {
  const [uiState, dispatch] = useReducer(reducer, initialState);
  const runtime = useAgentRuntime();
  const agent = runtime.state;
  const workbench = useWorkspaceWorkbench({ workspaceId: agent.workspace?.id || agent.activeTask?.workspace_id || "" });
  const { refreshTree, openFile } = workbench;
  const handledFileEvents = useRef<Set<string>>(new Set());
  const lastTerminalTask = useRef<string>("");

  useEffect(() => {
    const fileEvent = agent.events.items.find((event) => event.type === "file_changed" && metadataPath(event.metadata));
    if (!fileEvent) {
      return;
    }
    const key = fileEvent.id || `${fileEvent.created_at}:${fileEvent.content}`;
    if (handledFileEvents.current.has(key)) {
      return;
    }
    handledFileEvents.current.add(key);
    const path = metadataPath(fileEvent.metadata);
    void refreshTree().then(() => openFile(path));
  }, [agent.events.items, openFile, refreshTree]);

  useEffect(() => {
    const task = agent.activeTask;
    if (!task?.id || !["completed", "failed", "cancelled"].includes(task.status || "")) {
      return;
    }
    if (lastTerminalTask.current === task.id) {
      return;
    }
    lastTerminalTask.current = task.id;
    void refreshTree();
  }, [agent.activeTask, refreshTree]);

  const headerStatuses = useMemo<HeaderStatus[]>(() => {
    const health = agent.health.data;
    const database = health?.database;
    const backendOk = agent.health.status === "success" && ["ok", "healthy"].includes(health?.status || "");
    const taskStatus = agent.activeTask?.status || (agent.chatFallback ? "chat" : "idle");

    return [
      {
        label: "Backend",
        value: agent.health.status === "loading" ? "checking" : backendOk ? "online" : agent.health.status === "error" ? "offline" : "unchecked",
        detail: health?.service ? `${health.service} ${health.version || ""}`.trim() : "GET /api/v1/health",
        tone: backendOk ? "ok" : agent.health.status === "error" ? "danger" : "idle",
      },
      {
        label: "Database",
        value: database?.connected ? "connected" : agent.health.status === "success" ? "unknown" : "unchecked",
        detail: database?.path || database?.type || agent.health.error || "waiting for health check",
        tone: database?.connected ? "ok" : agent.health.status === "error" ? "danger" : "idle",
      },
      {
        label: "SSE",
        value: agent.streamStatus,
        detail: agent.streamError || (agent.activeTask?.id ? `/tasks/${agent.activeTask.id}/events` : "waiting for task"),
        tone: agent.streamStatus === "connected" ? "ok" : agent.streamStatus === "connecting" ? "warning" : agent.streamStatus === "error" ? "danger" : "idle",
      },
      {
        label: "Task",
        value: taskStatus,
        detail: agent.activeTask?.id || agent.chatFallback?.prompt || "no active task",
        tone: taskStatus === "failed" || taskStatus === "cancelled" ? "danger" : taskStatus === "completed" ? "ok" : taskStatus === "running" ? "warning" : "idle",
      },
    ];
  }, [agent.activeTask, agent.chatFallback, agent.health, agent.streamError, agent.streamStatus]);

  return (
    <div className="flex h-screen min-h-0 flex-col overflow-hidden bg-slate-950 text-slate-200">
      <TerminalHeader statuses={headerStatuses} onRefresh={runtime.refreshHealth} />

      <main className="flex min-h-0 flex-1 overflow-hidden border-y border-slate-800/90">
        <ActivityBar activeView={uiState.activeView} onChange={(view) => dispatch({ type: "setActiveView", view })} />
        {uiState.activeView === "history" ? (
          <HistoryPage
            history={agent.history}
            activeTaskId={agent.activeTask?.id || ""}
            onBack={() => dispatch({ type: "setActiveView", view: "explorer" })}
            onRefresh={runtime.refreshHistory}
            onSelectTask={(task) => {
              runtime.selectHistoryItem(task);
              dispatch({ type: "setActiveView", view: "explorer" });
            }}
          />
        ) : (
          <>
            <FileExplorer
              workspace={agent.workspace}
              tree={workbench.state.fileTree}
              expandedDirectories={workbench.state.expandedDirectories}
              activeFilePath={workbench.state.activeFilePath}
              onRefresh={workbench.refreshTree}
              onToggleDirectory={workbench.toggleDirectory}
              onOpenFile={workbench.openFile}
            />
            <CodeEditor
              files={workbench.state.openFiles}
              activeFilePath={workbench.state.activeFilePath}
              onSelectFile={workbench.selectFile}
              onCloseFile={workbench.closeFile}
            />
            <AgentChatPanel
              state={agent}
              onSubmit={runtime.submitTask}
              onCancel={runtime.cancelActiveTask}
              onResolvePermission={runtime.resolvePermission}
              onOpenFile={workbench.openFile}
            />
          </>
        )}
      </main>

      {uiState.activeView !== "history" ? (
        <BottomPanel agent={agent} panel={workbench.state.bottomPanel} onChange={workbench.setBottomPanel} />
      ) : null}

      <StatusBar
        sessionId={agent.session?.id || agent.activeTask?.session_id || "-"}
        workspaceId={agent.workspace?.id || agent.activeTask?.workspace_id || "-"}
        taskId={agent.activeTask?.id || "-"}
        apiBase="/api/v1"
        connection={agent.streamStatus === "connected" ? "sse" : agent.polling ? "polling" : agent.health.status === "success" ? "connected" : "idle"}
      />
    </div>
  );
}

function BottomPanel({
  agent,
  panel,
  onChange,
}: {
  agent: AgentState;
  panel: "terminal" | "output" | "problems" | "task-log" | null;
  onChange: (panel: "terminal" | "output" | "problems" | "task-log" | null) => void;
}) {
  const open = panel !== null;
  const labels = [
    { id: "terminal" as const, label: "Tool Calls" },
    { id: "output" as const, label: "File Changes" },
    { id: "task-log" as const, label: "Task Log" },
    { id: "problems" as const, label: "Replay" },
  ];

  return (
    <section className="shrink-0 border-t border-slate-800 bg-[#0b1220]">
      <div className="flex h-9 items-center gap-1 px-3">
        {labels.map((item) => (
          <button
            key={item.id}
            type="button"
            onClick={() => onChange(panel === item.id ? null : item.id)}
            className={`h-7 rounded px-3 font-mono text-xs ${panel === item.id ? "bg-cyan-400/10 text-cyan-200" : "text-slate-500 hover:bg-white/5 hover:text-slate-200"}`}
          >
            {item.label}
          </button>
        ))}
        <button
          type="button"
          onClick={() => onChange(open ? null : "terminal")}
          className="ml-auto grid h-7 w-7 place-items-center rounded text-slate-500 hover:bg-white/5 hover:text-slate-200"
          title={open ? "Collapse bottom panel" : "Expand bottom panel"}
        >
          {open ? <ChevronDown className="h-4 w-4" /> : <ChevronUp className="h-4 w-4" />}
        </button>
      </div>
      {open ? (
        <div className="custom-scrollbar h-44 overflow-auto border-t border-slate-800 p-3 font-mono text-xs text-slate-400">
          <pre className="whitespace-pre-wrap leading-5">
            {panel === "terminal"
              ? JSON.stringify(agent.toolCalls.items, null, 2)
              : panel === "output"
                ? JSON.stringify(agent.fileChanges.items, null, 2)
                : panel === "task-log"
                  ? agent.logs.items.map((log) => `[${log.type || "log"}] ${log.content || ""}`).join("\n\n")
                  : JSON.stringify(agent.replay.items, null, 2)}
          </pre>
        </div>
      ) : null}
    </section>
  );
}

function metadataPath(metadata: unknown) {
  if (!metadata || typeof metadata !== "object") {
    return "";
  }
  const path = (metadata as { path?: unknown }).path;
  return typeof path === "string" ? path : "";
}
