export const API_BASE = "/api/v1";

export const endpoints = {
  health: `${API_BASE}/health`,
  sessions: `${API_BASE}/sessions`,
  workspaces: `${API_BASE}/workspaces`,
  tasks: `${API_BASE}/tasks`,
  permissionsPending: `${API_BASE}/permissions`,
  chat: `${API_BASE}/chat`,
  chatHistory: `${API_BASE}/chat/history`,
  task(taskId: string) {
    return `${API_BASE}/tasks/${encodeURIComponent(taskId)}`;
  },
  workspaceTree(workspaceId: string) {
    return `${API_BASE}/workspaces/${encodeURIComponent(workspaceId)}/files/tree`;
  },
  workspaceFileContent(workspaceId: string) {
    return `${API_BASE}/workspaces/${encodeURIComponent(workspaceId)}/files/content`;
  },
  taskCancel(taskId: string) {
    return `${this.task(taskId)}/cancel`;
  },
  taskEvents(taskId: string) {
    return `${this.task(taskId)}/events`;
  },
  taskLogs(taskId: string) {
    return `${this.task(taskId)}/logs`;
  },
  taskToolCalls(taskId: string) {
    return `${this.task(taskId)}/tool-calls`;
  },
  taskFileChanges(taskId: string) {
    return `${this.task(taskId)}/file-changes`;
  },
  taskReplay(taskId: string) {
    return `${this.task(taskId)}/replay`;
  },
  permission(permissionId: string) {
    return `${API_BASE}/permissions/${encodeURIComponent(permissionId)}`;
  },
  permissionApprove(permissionId: string) {
    return `${this.permission(permissionId)}/approve`;
  },
  permissionReject(permissionId: string) {
    return `${this.permission(permissionId)}/reject`;
  },
};
