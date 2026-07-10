import { useMemo, useReducer } from "react";

import { AgentPanel } from "./components/agent/AgentPanel";
import { EditorShell } from "./components/editor/EditorShell";
import { HistoryPage } from "./components/history/HistoryPage";
import { ActivityBar } from "./components/layout/ActivityBar";
import { StatusBar } from "./components/layout/StatusBar";
import { TerminalHeader } from "./components/layout/TerminalHeader";
import { WorkspaceExplorer } from "./components/layout/WorkspaceExplorer";
import { useAgentRuntime } from "./hooks/useAgentRuntime";
import type { AppAction, AppState, HeaderStatus } from "./types";

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
        <ActivityBar
          activeView={uiState.activeView}
          onChange={(view) => dispatch({ type: "setActiveView", view })}
        />
        {uiState.activeView === "history" ? (
          <HistoryPage
            history={agent.history}
            activeTaskId={agent.activeTask?.id || ""}
            onBack={() => dispatch({ type: "setActiveView", view: "explorer" })}
            onRefresh={runtime.refreshHistory}
            onSelectTask={(task) => {
              runtime.selectHistoryItem(task);
              dispatch({ type: "setActiveView", view: "explorer" });
              dispatch({ type: "setActiveEditorTab", tab: "overview" });
            }}
          />
        ) : (
          <>
            <WorkspaceExplorer
              activeView={uiState.activeView}
              history={agent.history}
              permissions={agent.permissions}
              logs={agent.logs}
              toolCalls={agent.toolCalls}
              fileChanges={agent.fileChanges}
              activeTask={agent.activeTask}
              activeTaskId={agent.activeTask?.id || ""}
              onRefreshHistory={runtime.refreshHistory}
              onSelectHistoryItem={(item) => {
                runtime.selectHistoryItem(item);
                dispatch({ type: "setActiveView", view: "explorer" });
                dispatch({ type: "setActiveEditorTab", tab: "overview" });
              }}
            />
            <EditorShell
              className={uiState.activeView === "permissions" ? "hidden xl:flex" : "flex"}
              activeTab={uiState.activeEditorTab}
              onSelectTab={(tab) => dispatch({ type: "setActiveEditorTab", tab })}
              task={agent.activeTask}
              chatFallback={agent.chatFallback}
              logs={agent.logs}
              toolCalls={agent.toolCalls}
              fileChanges={agent.fileChanges}
              replay={agent.replay}
            />
            <AgentPanel
              className={uiState.activeView === "permissions" ? "flex flex-1" : "hidden xl:flex"}
              state={agent}
              onSubmitTask={runtime.submitTask}
              onCancelTask={runtime.cancelActiveTask}
              onRefreshHistory={runtime.refreshHistory}
              onResolvePermission={runtime.resolvePermission}
            />
          </>
        )}
      </main>

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
