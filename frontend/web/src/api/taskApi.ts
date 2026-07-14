import { isEndpointUnavailable, requestJson } from "./client";
import { endpoints } from "./endpoints";
import type {
  ChatHistoryPayload,
  ChatMessageRecord,
  CreateTaskInput,
  FileChangeRecord,
  ListState,
  LogRecord,
  ReplayPayload,
  SessionRecord,
  TaskListPayload,
  TaskRecord,
  TaskEventHistoryPayload,
  ToolCallRecord,
  WorkspaceRecord,
  LocalDirectorySelection,
} from "../types/api";

export function createSession(title: string) {
  return requestJson<SessionRecord>(endpoints.sessions, {
    method: "POST",
    body: JSON.stringify({ title }),
  });
}

export function createWorkspace(name: string, path: string) {
  return requestJson<WorkspaceRecord>(endpoints.workspaces, {
    method: "POST",
    body: JSON.stringify({ name, path }),
  });
}

export function selectLocalWorkspaceDirectory() {
  return requestJson<LocalDirectorySelection>(`${endpoints.workspaces}/select-directory`, {
    method: "POST",
  });
}

export function createTask(
  sessionId: string,
  workspaceId: string,
  input: string,
  options: Pick<CreateTaskInput, "autoRunSafeCommands" | "requireFileWritePermission" | "maxSteps" | "executionMode">,
) {
  return requestJson<TaskRecord>(endpoints.tasks, {
    method: "POST",
    body: JSON.stringify({
      session_id: sessionId,
      workspace_id: workspaceId,
      input,
      options: {
        auto_run_safe_commands: options.autoRunSafeCommands,
        require_permission_for_file_write: options.requireFileWritePermission,
        max_steps: options.maxSteps,
        execution_mode: options.executionMode,
      },
    }),
  });
}

export function getTask(taskId: string) {
  return requestJson<TaskRecord>(endpoints.task(taskId));
}

export function listTasks(pageSize = 50) {
  return requestJson<TaskListPayload>(`${endpoints.tasks}?page=1&page_size=${pageSize}`);
}

export function cancelTask(taskId: string) {
  return requestJson<TaskRecord>(endpoints.taskCancel(taskId), {
    method: "POST",
    body: JSON.stringify({ reason: "user_cancelled" }),
  });
}

export function submitChatFallback(prompt: string) {
  return requestJson<ChatMessageRecord>(endpoints.chat, {
    method: "POST",
    body: JSON.stringify({ prompt }),
  });
}

export function listChatHistory() {
  return requestJson<ChatHistoryPayload>(endpoints.chatHistory);
}

export function listLogs(taskId: string) {
  return requestJson<{ items: LogRecord[] }>(endpoints.taskLogs(taskId));
}

export function listToolCalls(taskId: string) {
  return requestJson<{ items: ToolCallRecord[] }>(endpoints.taskToolCalls(taskId));
}

export function listFileChanges(taskId: string) {
  return requestJson<{ items: FileChangeRecord[] }>(endpoints.taskFileChanges(taskId));
}

export function getReplay(taskId: string) {
  return requestJson<ReplayPayload>(endpoints.taskReplay(taskId));
}

export function listEventHistory(taskId: string) {
  return requestJson<TaskEventHistoryPayload>(`${endpoints.taskEvents(taskId)}/history`);
}

export async function safeListArtifacts(taskId: string) {
  const empty = <T>(): ListState<T> => ({ status: "success", items: [], error: "" });
  const [logs, tools, changes, replay] = await Promise.all([
    listLogs(taskId).catch((error) => (isEndpointUnavailable(error) ? { items: [] } : Promise.reject(error))),
    listToolCalls(taskId).catch((error) => (isEndpointUnavailable(error) ? { items: [] } : Promise.reject(error))),
    listFileChanges(taskId).catch((error) => (isEndpointUnavailable(error) ? { items: [] } : Promise.reject(error))),
    getReplay(taskId).catch((error) => (isEndpointUnavailable(error) ? { timeline: [] } : Promise.reject(error))),
  ]);

  return {
    logs: { ...empty<LogRecord>(), items: logs.items || [] },
    tools: { ...empty<ToolCallRecord>(), items: tools.items || [] },
    changes: { ...empty<FileChangeRecord>(), items: changes.items || [] },
    replay: { ...empty<NonNullable<ReplayPayload["timeline"]>[number]>(), items: replay.timeline || [] },
  };
}
