import { useCallback, useEffect, useReducer, useRef } from "react";

import { getHealth } from "../api/healthApi";
import { approvePermission, listPendingPermissions, rejectPermission } from "../api/permissionApi";
import {
  cancelTask,
  createSession,
  createTask,
  createWorkspace,
  getTask,
  listEventHistory,
  listChatHistory,
  listTasks,
  safeListArtifacts,
  submitChatFallback,
} from "../api/taskApi";
import { endpoints } from "../api/endpoints";
import {
  agentReducer,
  initialAgentState,
  isTerminalTask,
  TERMINAL_TASK_STATUSES,
} from "../store/agentReducer";
import type { ChatMessageRecord, CreateTaskInput, SessionRecord, TaskEventRecord, TaskRecord, WorkspaceRecord } from "../types/api";
import { isEndpointUnavailable } from "../api/client";

function messageFromError(error: unknown) {
  return error instanceof Error ? error.message : String(error);
}

function isTaskRecord(item: TaskRecord | ChatMessageRecord): item is TaskRecord {
  return typeof (item as TaskRecord).status === "string" || typeof (item as TaskRecord).session_id === "string";
}

export function useAgentRuntime(options: {
  onFileChanged?: (path: string) => void | Promise<void>;
  onTaskSettled?: () => void | Promise<void>;
} = {}) {
  const [state, dispatch] = useReducer(agentReducer, initialAgentState);
  const pollTimer = useRef<number | null>(null);
  const activePollTaskId = useRef<string>("");
  const pollInFlight = useRef(false);
  const permissionTimer = useRef<number | null>(null);
  const eventSource = useRef<EventSource | null>(null);
  const activeStreamTaskId = useRef<string>("");
  const seenEventIds = useRef<Set<string>>(new Set());
  const sessionRef = useRef<SessionRecord | null>(null);
  const workspaceRef = useRef<WorkspaceRecord | null>(null);

  const stopPolling = useCallback(() => {
    if (pollTimer.current !== null) {
      window.clearInterval(pollTimer.current);
      pollTimer.current = null;
    }
    activePollTaskId.current = "";
    pollInFlight.current = false;
    dispatch({ type: "polling", active: false });
  }, []);

  const closeEventStream = useCallback((status: "idle" | "closed" | "error" = "closed") => {
    if (eventSource.current) {
      eventSource.current.close();
      eventSource.current = null;
    }
    activeStreamTaskId.current = "";
    seenEventIds.current.clear();
    dispatch({ type: "streamStatus", status });
  }, []);

  const stopPermissionPolling = useCallback(() => {
    if (permissionTimer.current !== null) {
      window.clearInterval(permissionTimer.current);
      permissionTimer.current = null;
    }
  }, []);

  const refreshHealth = useCallback(async () => {
    dispatch({ type: "healthLoading" });
    try {
      const health = await getHealth();
      dispatch({ type: "healthSuccess", health });
    } catch (error) {
      dispatch({ type: "healthError", error: messageFromError(error) });
    }
  }, []);

  const refreshHistory = useCallback(async () => {
    dispatch({ type: "historyLoading" });
    try {
      const tasks = await listTasks();
      dispatch({ type: "historySuccess", items: tasks.items || [] });
    } catch (taskError) {
      if (!isEndpointUnavailable(taskError)) {
        dispatch({ type: "historyError", error: messageFromError(taskError) });
        return;
      }

      try {
        const chat = await listChatHistory();
        dispatch({ type: "historySuccess", items: chat.items || [] });
      } catch (chatError) {
        dispatch({ type: "historyError", error: messageFromError(chatError) });
      }
    }
  }, []);

  const refreshArtifacts = useCallback(async (taskId: string) => {
    try {
      const artifacts = await safeListArtifacts(taskId);
      dispatch({
        type: "artifactsSuccess",
        logs: artifacts.logs,
        toolCalls: artifacts.tools,
        fileChanges: artifacts.changes,
        replay: artifacts.replay,
      });
    } catch (error) {
      dispatch({ type: "artifactsError", error: messageFromError(error) });
    }
  }, []);

  const refreshEventHistory = useCallback(async (taskId: string) => {
    try {
      const history = await listEventHistory(taskId);
      dispatch({ type: "eventHistorySuccess", events: history.items || [] });
    } catch (error) {
      if (!isEndpointUnavailable(error)) {
        dispatch({ type: "eventHistoryError", error: messageFromError(error) });
      }
    }
  }, []);

  const refreshPermissions = useCallback(async (taskId?: string) => {
    dispatch({ type: "permissionsLoading" });
    try {
      const permissions = await listPendingPermissions(taskId);
      dispatch({ type: "permissionsSuccess", items: permissions.items || [] });
    } catch (error) {
      dispatch({ type: "permissionsError", error: messageFromError(error) });
    }
  }, []);

  const ensureRuntimeContext = useCallback(
    async (input: CreateTaskInput) => {
      if (sessionRef.current && workspaceRef.current) {
        return { session: sessionRef.current, workspace: workspaceRef.current };
      }

      const session = await createSession(input.sessionTitle.trim() || "CodePilot Session");
      const workspace = await createWorkspace(
        input.workspaceName.trim() || "codepilot-workspace",
        input.workspacePath.trim() || "/workspace",
      );
      sessionRef.current = session;
      workspaceRef.current = workspace;
      return { session, workspace };
    },
    [],
  );

  const startPermissionPolling = useCallback(
    (taskId?: string) => {
      stopPermissionPolling();
      void refreshPermissions(taskId);
      permissionTimer.current = window.setInterval(() => {
        void refreshPermissions(taskId);
      }, 3500);
    },
    [refreshPermissions, stopPermissionPolling],
  );

  const pollTask = useCallback(
    (taskId: string) => {
      stopPolling();
      activePollTaskId.current = taskId;
      dispatch({ type: "polling", active: true });

      const tick = async () => {
        if (activePollTaskId.current !== taskId || pollInFlight.current) {
          return;
        }

        pollInFlight.current = true;
        try {
          const task = await getTask(taskId);
          dispatch({ type: "taskUpdated", task });
          void refreshArtifacts(taskId);
          void refreshPermissions(taskId);

          if (task.status && TERMINAL_TASK_STATUSES.has(task.status)) {
            stopPolling();
            stopPermissionPolling();
            void refreshHistory();
            void options.onTaskSettled?.();
            return;
          }
        } catch (error) {
          dispatch({ type: "submitError", error: messageFromError(error) });
          stopPolling();
        } finally {
          pollInFlight.current = false;
        }
      };

      void tick();
      pollTimer.current = window.setInterval(tick, 2500);
    },
    [options, refreshArtifacts, refreshHistory, stopPolling],
  );

  const startEventStream = useCallback(
    (taskId: string) => {
      closeEventStream("idle");
      stopPolling();
      dispatch({ type: "eventsReset" });

      if (!window.EventSource) {
        dispatch({ type: "streamStatus", status: "unsupported", error: "EventSource is not supported by this browser." });
        pollTask(taskId);
        return;
      }

      const source = new EventSource(endpoints.taskEvents(taskId));
      eventSource.current = source;
      activeStreamTaskId.current = taskId;
      seenEventIds.current.clear();
      dispatch({ type: "streamStatus", status: "connecting" });

      const handleEvent = (eventName: string, message: MessageEvent<string>) => {
        if (activeStreamTaskId.current !== taskId) {
          return;
        }

        const event = parseSseEvent(eventName, message);
        const eventId = event.id || message.lastEventId || `${event.type || eventName}:${event.created_at || ""}:${event.content || ""}`;
        if (seenEventIds.current.has(eventId)) {
          return;
        }
        seenEventIds.current.add(eventId);

        dispatch({ type: "eventReceived", event: { ...event, id: event.id || eventId } });

        if (event.type === "permission_required" || event.type === "tool_finished" || event.type === "file_changed") {
            void refreshArtifacts(taskId);
          void refreshPermissions(taskId);
        }

        if (event.type === "file_changed") {
          const path = metadataPath(event.metadata);
          if (path) {
            void options.onFileChanged?.(path);
          }
        }

        if (event.type === "task_completed" || event.type === "task_failed" || event.type === "task_cancelled") {
          closeEventStream("closed");
          stopPolling();
          stopPermissionPolling();
          void refreshArtifacts(taskId);
          void refreshPermissions(taskId);
          void refreshHistory();
          void options.onTaskSettled?.();
          void getTask(taskId).then((task) => dispatch({ type: "taskUpdated", task })).catch(() => undefined);
        }

        if (event.type === "stream_end") {
          closeEventStream("closed");
          void getTask(taskId)
            .then((task) => {
              dispatch({ type: "taskUpdated", task });
              void refreshArtifacts(taskId);
              void refreshPermissions(taskId);
              if (isTerminalTask(task)) {
                stopPolling();
                stopPermissionPolling();
                void refreshHistory();
                void options.onTaskSettled?.();
              } else {
                pollTask(taskId);
              }
            })
            .catch(() => pollTask(taskId));
        }
      };

      source.onopen = () => {
        if (activeStreamTaskId.current === taskId) {
          dispatch({ type: "streamStatus", status: "connected" });
        }
      };

      source.onmessage = (message) => handleEvent("message", message);

      for (const eventName of SSE_EVENT_NAMES) {
        source.addEventListener(eventName, (message) => handleEvent(eventName, message as MessageEvent<string>));
      }

      source.onerror = () => {
        if (activeStreamTaskId.current !== taskId) {
          return;
        }
        closeEventStream("error");
        dispatch({ type: "streamStatus", status: "error", error: "SSE disconnected; task polling is active." });
        pollTask(taskId);
      };
    },
    [closeEventStream, options, pollTask, refreshArtifacts, refreshHistory, stopPolling],
  );

  const submitTask = useCallback(
    async (input: CreateTaskInput) => {
      closeEventStream("idle");
      stopPolling();
      dispatch({
        type: "submitStart",
        prompt: input.taskInput.trim(),
        startedAt: new Date().toISOString(),
      });

      try {
        const { session, workspace } = await ensureRuntimeContext(input);

        try {
          const task = await createTask(session.id, workspace.id, input.taskInput, {
            autoRunSafeCommands: input.autoRunSafeCommands,
            requireFileWritePermission: input.requireFileWritePermission,
            maxSteps: input.maxSteps,
            executionMode: input.executionMode,
          });
          dispatch({ type: "submitSuccess", session, workspace, task });
          if (!isTerminalTask(task)) {
            startEventStream(task.id);
          }
          void refreshHistory();
          void refreshArtifacts(task.id);
          void refreshEventHistory(task.id);
          startPermissionPolling(task.id);
        } catch (taskError) {
          if (!isEndpointUnavailable(taskError)) {
            throw taskError;
          }

          const chat = await submitChatFallback(input.taskInput);
          dispatch({ type: "chatFallbackSuccess", session, workspace, chat });
          void refreshHistory();
        }
      } catch (error) {
        dispatch({ type: "submitError", error: messageFromError(error) });
      }
    },
    [closeEventStream, ensureRuntimeContext, refreshArtifacts, refreshEventHistory, refreshHistory, startEventStream, startPermissionPolling, stopPolling],
  );

  const cancelActiveTask = useCallback(async () => {
    const taskId = state.activeTask?.id;
    if (!taskId) {
      return;
    }

    dispatch({ type: "cancelStart" });
    try {
      const task = await cancelTask(taskId);
      dispatch({ type: "cancelDone", task });
      closeEventStream("closed");
      stopPolling();
      stopPermissionPolling();
      void refreshHistory();
      void refreshArtifacts(taskId);
      void refreshPermissions(taskId);
    } catch (error) {
      dispatch({ type: "cancelError", error: messageFromError(error) });
    }
  }, [closeEventStream, refreshArtifacts, refreshHistory, refreshPermissions, state.activeTask?.id, stopPermissionPolling, stopPolling]);

  const selectHistoryItem = useCallback(
    async (item: TaskRecord | ChatMessageRecord) => {
      if (!isTaskRecord(item)) {
        return;
      }

      stopPolling();
      stopPermissionPolling();
      closeEventStream("idle");
      dispatch({ type: "selectTask", task: item });

      try {
        const fullTask = await getTask(item.id);
        dispatch({ type: "taskUpdated", task: fullTask });
        void refreshArtifacts(fullTask.id);
        void refreshEventHistory(fullTask.id);
        startPermissionPolling(fullTask.id);
        if (!isTerminalTask(fullTask)) {
          startEventStream(fullTask.id);
        }
      } catch {
        void refreshArtifacts(item.id);
        void refreshEventHistory(item.id);
        startPermissionPolling(item.id);
        if (!isTerminalTask(item)) {
          startEventStream(item.id);
        }
      }
    },
    [closeEventStream, refreshArtifacts, refreshEventHistory, startEventStream, startPermissionPolling, stopPermissionPolling, stopPolling],
  );

  const resolvePermission = useCallback(
    async (permissionId: string, approved: boolean) => {
      dispatch({ type: "permissionResolving", permissionId, resolving: true });
      try {
        if (approved) {
          await approvePermission(permissionId);
        } else {
          await rejectPermission(permissionId);
        }
        const taskId = state.activeTask?.id;
        await refreshPermissions(taskId);
        if (taskId) {
          void refreshArtifacts(taskId);
        }
      } catch (error) {
        dispatch({ type: "permissionsError", error: messageFromError(error) });
      } finally {
        dispatch({ type: "permissionResolving", permissionId, resolving: false });
      }
    },
    [refreshArtifacts, refreshPermissions, state.activeTask?.id],
  );

  useEffect(() => {
    void refreshHealth();
    void refreshHistory();
    void refreshPermissions();
    return () => {
      closeEventStream("closed");
      stopPermissionPolling();
      stopPolling();
    };
  }, [closeEventStream, refreshHealth, refreshHistory, refreshPermissions, stopPermissionPolling, stopPolling]);

  useEffect(() => {
    sessionRef.current = state.session;
    workspaceRef.current = state.workspace;
  }, [state.session, state.workspace]);

  return {
    state,
    refreshHealth,
    refreshHistory,
    refreshArtifacts,
    submitTask,
    cancelActiveTask,
    selectHistoryItem,
    refreshPermissions,
    resolvePermission,
  };
}

const SSE_EVENT_NAMES = [
  "task_created",
  "task_planning",
  "agent_message",
  "tool_started",
  "tool_output",
  "tool_finished",
  "permission_required",
  "permission_resolved",
  "file_changed",
  "task_completed",
  "task_failed",
  "task_cancelled",
  "stream_end",
];

function parseSseEvent(eventName: string, message: MessageEvent<string>): TaskEventRecord {
  try {
    const parsed = JSON.parse(message.data) as TaskEventRecord;
    return {
      ...parsed,
      type: parsed.type || eventName,
    };
  } catch {
    return {
      id: message.lastEventId || undefined,
      type: eventName,
      content: message.data,
    };
  }
}

function metadataPath(metadata: unknown) {
  if (!metadata || typeof metadata !== "object") {
    return "";
  }
  const path = (metadata as { path?: unknown }).path;
  return typeof path === "string" ? path : "";
}
