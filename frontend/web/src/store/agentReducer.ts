import type {
  ChatMessageRecord,
  FileChangeRecord,
  AgentStepRecord,
  HealthPayload,
  ListState,
  LogRecord,
  PermissionRecord,
  ReplayPayload,
  SessionRecord,
  TaskEventRecord,
  TaskRecord,
  ToolCallRecord,
  ValueState,
  WorkspaceRecord,
} from "../types/api";

type ReplayItem = NonNullable<ReplayPayload["timeline"]>[number];

export interface AgentState {
  health: ValueState<HealthPayload>;
  session: SessionRecord | null;
  workspace: WorkspaceRecord | null;
  activeTask: TaskRecord | null;
  chatFallback: ChatMessageRecord | null;
  history: ListState<TaskRecord | ChatMessageRecord>;
  logs: ListState<LogRecord>;
  toolCalls: ListState<ToolCallRecord>;
  fileChanges: ListState<FileChangeRecord>;
  replay: ListState<ReplayItem>;
  permissions: ListState<PermissionRecord>;
  resolvingPermissionIds: string[];
  events: ListState<TaskEventRecord>;
  steps: AgentStepRecord[];
  streamStatus: "idle" | "connecting" | "connected" | "error" | "closed" | "unsupported";
  streamError: string;
  submitting: boolean;
  cancelling: boolean;
  polling: boolean;
  error: string;
}

export type AgentAction =
  | { type: "healthLoading" }
  | { type: "healthSuccess"; health: HealthPayload }
  | { type: "healthError"; error: string }
  | { type: "historyLoading" }
  | { type: "historySuccess"; items: Array<TaskRecord | ChatMessageRecord> }
  | { type: "historyError"; error: string }
  | { type: "submitStart" }
  | { type: "submitError"; error: string }
  | { type: "submitSuccess"; session: SessionRecord | null; workspace: WorkspaceRecord | null; task: TaskRecord }
  | { type: "chatFallbackSuccess"; session: SessionRecord | null; workspace: WorkspaceRecord | null; chat: ChatMessageRecord }
  | { type: "taskUpdated"; task: TaskRecord }
  | { type: "selectTask"; task: TaskRecord }
  | { type: "cancelStart" }
  | { type: "cancelDone"; task: TaskRecord }
  | { type: "cancelError"; error: string }
  | { type: "polling"; active: boolean }
  | { type: "permissionsLoading" }
  | { type: "permissionsSuccess"; items: PermissionRecord[] }
  | { type: "permissionsError"; error: string }
  | { type: "permissionResolving"; permissionId: string; resolving: boolean }
  | { type: "streamStatus"; status: AgentState["streamStatus"]; error?: string }
  | { type: "eventReceived"; event: TaskEventRecord }
  | { type: "eventHistorySuccess"; events: TaskEventRecord[] }
  | { type: "eventHistoryError"; error: string }
  | { type: "eventsReset" }
  | {
      type: "artifactsSuccess";
      logs: ListState<LogRecord>;
      toolCalls: ListState<ToolCallRecord>;
      fileChanges: ListState<FileChangeRecord>;
      replay: ListState<ReplayItem>;
    }
  | { type: "artifactsError"; error: string };

function idleList<T>(): ListState<T> {
  return { status: "idle", items: [], error: "" };
}

export const initialAgentState: AgentState = {
  health: { status: "idle", data: null, error: "" },
  session: null,
  workspace: null,
  activeTask: null,
  chatFallback: null,
  history: idleList<TaskRecord | ChatMessageRecord>(),
  logs: idleList<LogRecord>(),
  toolCalls: idleList<ToolCallRecord>(),
  fileChanges: idleList<FileChangeRecord>(),
  replay: idleList<ReplayItem>(),
  permissions: idleList<PermissionRecord>(),
  resolvingPermissionIds: [],
  events: idleList<TaskEventRecord>(),
  steps: [],
  streamStatus: "idle",
  streamError: "",
  submitting: false,
  cancelling: false,
  polling: false,
  error: "",
};

export const TERMINAL_TASK_STATUSES = new Set(["completed", "failed", "cancelled"]);
const TERMINAL_EVENT_STATUS: Record<string, string> = {
  task_completed: "completed",
  task_failed: "failed",
  task_cancelled: "cancelled",
};

export function isTerminalTask(task: TaskRecord | null) {
  return Boolean(task?.status && TERMINAL_TASK_STATUSES.has(task.status));
}

export function agentReducer(state: AgentState, action: AgentAction): AgentState {
  switch (action.type) {
    case "healthLoading":
      return { ...state, health: { status: "loading", data: state.health.data, error: "" } };
    case "healthSuccess":
      return { ...state, health: { status: "success", data: action.health, error: "" } };
    case "healthError":
      return { ...state, health: { status: "error", data: null, error: action.error } };
    case "historyLoading":
      return { ...state, history: { ...state.history, status: "loading", error: "" } };
    case "historySuccess":
      return { ...state, history: { status: "success", items: action.items, error: "" } };
    case "historyError":
      return { ...state, history: { status: "error", items: [], error: action.error } };
    case "submitStart":
      return {
        ...state,
        submitting: true,
        chatFallback: null,
        error: "",
        logs: { status: "loading", items: [], error: "" },
        toolCalls: { status: "loading", items: [], error: "" },
        fileChanges: { status: "loading", items: [], error: "" },
        replay: { status: "loading", items: [], error: "" },
        permissions: { status: "loading", items: [], error: "" },
        events: { status: "idle", items: [], error: "" },
        steps: [],
        streamStatus: "idle",
        streamError: "",
      };
    case "submitError":
      return { ...state, submitting: false, polling: false, error: action.error };
    case "submitSuccess":
      return {
        ...state,
        submitting: false,
        session: action.session,
        workspace: action.workspace,
        activeTask: action.task,
        chatFallback: null,
        error: "",
      };
    case "chatFallbackSuccess":
      return {
        ...state,
        submitting: false,
        session: action.session,
        workspace: action.workspace,
        activeTask: null,
        chatFallback: action.chat,
        polling: false,
        error: "",
      };
    case "taskUpdated":
      return { ...state, activeTask: action.task, polling: !isTerminalTask(action.task), error: "" };
    case "selectTask":
      return {
        ...state,
        activeTask: action.task,
        chatFallback: null,
        error: "",
        logs: { status: "loading", items: [], error: "" },
        toolCalls: { status: "loading", items: [], error: "" },
        fileChanges: { status: "loading", items: [], error: "" },
        replay: { status: "loading", items: [], error: "" },
        permissions: { status: "loading", items: [], error: "" },
        events: { status: "idle", items: [], error: "" },
        steps: [],
        streamStatus: "idle",
        streamError: "",
      };
    case "cancelStart":
      return { ...state, cancelling: true, error: "" };
    case "cancelDone":
      return { ...state, cancelling: false, activeTask: action.task, polling: false, error: "" };
    case "cancelError":
      return { ...state, cancelling: false, error: action.error };
    case "polling":
      return { ...state, polling: action.active };
    case "permissionsLoading":
      return { ...state, permissions: { ...state.permissions, status: "loading", error: "" } };
    case "permissionsSuccess":
      return { ...state, permissions: { status: "success", items: action.items, error: "" } };
    case "permissionsError":
      return { ...state, permissions: { status: "error", items: [], error: action.error } };
    case "permissionResolving":
      return {
        ...state,
        resolvingPermissionIds: action.resolving
          ? Array.from(new Set([...state.resolvingPermissionIds, action.permissionId]))
          : state.resolvingPermissionIds.filter((id) => id !== action.permissionId),
      };
    case "streamStatus":
      return { ...state, streamStatus: action.status, streamError: action.error || "" };
    case "eventsReset":
      return { ...state, events: idleList<TaskEventRecord>(), steps: [] };
    case "eventReceived": {
      const nextEvents = [action.event, ...state.events.items].slice(0, 200);
      const nextTask = eventToTask(state.activeTask, action.event);
      return {
        ...state,
        activeTask: nextTask,
        events: { status: "success", items: nextEvents, error: "" },
        steps: eventsToSteps(nextEvents),
        polling: nextTask ? !isTerminalTask(nextTask) && state.polling : state.polling,
        error: "",
      };
    }
    case "eventHistorySuccess": {
      const unique = uniqueEvents(action.events).slice(-200).reverse();
      return {
        ...state,
        events: { status: "success", items: unique, error: "" },
        steps: eventsToSteps(unique),
        error: "",
      };
    }
    case "eventHistoryError":
      return { ...state, events: { status: "error", items: [], error: action.error }, steps: [] };
    case "artifactsSuccess":
      return {
        ...state,
        logs: action.logs,
        toolCalls: action.toolCalls,
        fileChanges: action.fileChanges,
        replay: action.replay,
      };
    case "artifactsError":
      return {
        ...state,
        logs: { status: "error", items: [], error: action.error },
        toolCalls: { status: "error", items: [], error: action.error },
        fileChanges: { status: "error", items: [], error: action.error },
        replay: { status: "error", items: [], error: action.error },
      };
    default:
      return state;
  }
}

function uniqueEvents(events: TaskEventRecord[]) {
  const seen = new Set<string>();
  const result: TaskEventRecord[] = [];
  for (const event of events) {
    const key = event.id || `${event.type || "event"}:${event.created_at || ""}:${event.content || ""}`;
    if (seen.has(key)) {
      continue;
    }
    seen.add(key);
    result.push(event);
  }
  return result;
}

function eventToTask(task: TaskRecord | null, event: TaskEventRecord): TaskRecord | null {
  if (!task) {
    return task;
  }

  const type = event.type || "";
  const nextStatus = TERMINAL_EVENT_STATUS[type];
  if (!nextStatus) {
    return task;
  }

  return {
    ...task,
    status: nextStatus,
    updated_at: event.created_at || task.updated_at,
  };
}

function eventsToSteps(events: TaskEventRecord[]): AgentStepRecord[] {
  return events
    .slice()
    .reverse()
    .map((event, index) => {
      const eventType = event.type || "message";
      return {
        id: event.id || `${eventType}-${index}`,
        title: titleForEvent(eventType, index),
        description: event.content || "No event content returned.",
        status: statusForEvent(eventType),
        eventType,
        createdAt: event.created_at,
      };
    });
}

function titleForEvent(type: string, index: number) {
  const labels: Record<string, string> = {
    task_created: "Task created",
    task_planning: "Planning",
    agent_message: "Agent message",
    tool_started: "Tool started",
    tool_output: "Tool output",
    tool_finished: "Tool finished",
    permission_required: "Permission required",
    permission_resolved: "Permission resolved",
    file_changed: "File changed",
    task_completed: "Task completed",
    task_failed: "Task failed",
    task_cancelled: "Task cancelled",
    stream_end: "Stream ended",
  };

  return labels[type] || `Event ${index + 1}`;
}

function statusForEvent(type: string): AgentStepRecord["status"] {
  if (type.includes("failed") || type.includes("cancelled")) {
    return "failed";
  }
  if (type.includes("completed") || type.includes("finished") || type.includes("resolved")) {
    return "success";
  }
  if (type.includes("permission")) {
    return "waiting";
  }
  if (type.includes("created") || type === "stream_end") {
    return "idle";
  }
  return "running";
}
