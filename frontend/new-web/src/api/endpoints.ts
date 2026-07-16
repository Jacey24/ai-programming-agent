const BASE = '/api/v1';

export const endpoints = {
  health: `${BASE}/health`,

  // Workspaces
  workspaces: `${BASE}/workspaces`,
  workspace: (id: string) => `${BASE}/workspaces/${id}`,
  workspaceFiles: (id: string) => `${BASE}/workspaces/${id}/files/tree`,
  workspaceFileContent: (id: string) =>
      `${BASE}/workspaces/${id}/files/content`,
  workspaceSessions: (id: string) => `${BASE}/workspaces/${id}/sessions`,
  selectDirectory: `${BASE}/workspaces/select-directory`,

  // Sessions
  sessions: `${BASE}/sessions`,
  session: (id: string) => `${BASE}/sessions/${id}`,

  // Tasks
  tasks: `${BASE}/tasks`,
  task: (id: string) => `${BASE}/tasks/${id}`,
  taskContinue: `${BASE}/tasks/continue`,
  taskEvents: (id: string) => `${BASE}/tasks/${id}/events`,
  taskEventHistory: (id: string) => `${BASE}/tasks/${id}/events/history`,
  taskToolCalls: (id: string) => `${BASE}/tasks/${id}/tool-calls`,
  taskLogs: (id: string) => `${BASE}/tasks/${id}/logs`,
  taskFileChanges: (id: string) => `${BASE}/tasks/${id}/file-changes`,
  taskReplay: (id: string) => `${BASE}/tasks/${id}/replay`,
  cancelTask: (id: string) => `${BASE}/tasks/${id}/cancel`,

  // Permissions
  permissions: `${BASE}/permissions`,
  permissionsPending: `${BASE}/permissions/pending`,
  permission: (id: string) => `${BASE}/permissions/${id}`,
  approvePermission: (id: string) => `${BASE}/permissions/${id}/approve`,
  rejectPermission: (id: string) => `${BASE}/permissions/${id}/reject`,

  // Tools
  tools: `${BASE}/tools`,
  tool: (name: string) => `${BASE}/tools/${name}`,

  // Experts
  experts: `${BASE}/experts`,
  expert: (name: string) => `${BASE}/experts/${name}`,
  expertTools: (name: string) => `${BASE}/experts/${name}/tools`,
  expertTool: (name: string, tool: string) =>
      `${BASE}/experts/${name}/tools/${tool}`,
  expertRoutes: (name: string) => `${BASE}/experts/${name}/routes`,
  expertRoute: (name: string, index: number) =>
      `${BASE}/experts/${name}/routes/${index}`,
  expertLlm: (name: string) => `${BASE}/experts/${name}/llm`,
  expertPromptPreview: (name: string) =>
      `${BASE}/experts/${name}/prompt/preview`,
  expertsGraph: `${BASE}/experts/graph`,
  expertsGraphPositions: `${BASE}/experts/graph/positions`,
  expertsExport: `${BASE}/experts/export`,
  expertsImport: `${BASE}/experts/import`,
  expertsValidate: `${BASE}/experts/validate`,
  expertsLlmDefaults: `${BASE}/experts/llm/defaults`,

  // Config
  config: `${BASE}/config`,
  configAgent: `${BASE}/config/agent`,
  configLlm: `${BASE}/config/llm`,
  configLlmTest: `${BASE}/config/llm/test`,
  configLlmProviders: `${BASE}/config/llm/providers`,
  configLlmProvider: (id: string) => `${BASE}/config/llm/providers/${id}`,
  configLlmLocal: `${BASE}/config/llm/local`,
  configWorkspace: `${BASE}/config/workspace`,
  configLogging: `${BASE}/config/logging`,
  configTools: `${BASE}/config/tools`,

  // Debug
  debugConsole: `${BASE}/debug/console`,
  debugState: `${BASE}/debug/state`,
  debugEvents: `${BASE}/debug/events`,
  debugTools: `${BASE}/debug/tools`,

  // Global
  globalSessions: `${BASE}/global/sessions`,
  globalTasks: `${BASE}/global/tasks`,
};