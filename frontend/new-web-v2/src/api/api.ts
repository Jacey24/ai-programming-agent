import type {ActiveTaskState, ConfigMergeView, ExpertDetail, ExpertGraph, ExpertRouteRule, ExpertSummary, FileChange, FileTreeNode, HealthResponse, LlmProvider, PermissionRecord, SessionRecord, TaskEventRecord, TaskLog, TaskRecord, ToolCallLog, ToolInfo, WorkspaceRecord,} from '../types';

import {requestJson,} from './client';
import {endpoints,} from './endpoints';

// ==================== Health ====================
export const getHealth = () => requestJson<HealthResponse>(endpoints.health);

// ==================== Workspaces ====================
export const listWorkspaces = () =>
    requestJson<{items: WorkspaceRecord[]}>(endpoints.workspaces);

export const getWorkspace = (id: string) =>
    requestJson<WorkspaceRecord>(endpoints.workspace(id));

export const createWorkspace = (name: string, path: string) =>
    requestJson<WorkspaceRecord>(endpoints.workspaces, {
      method: 'POST',
      body: JSON.stringify({name, path}),
    });

export const updateWorkspace = (id: string, data: Partial<WorkspaceRecord>) =>
    requestJson<WorkspaceRecord>(endpoints.workspace(id), {
      method: 'PUT',
      body: JSON.stringify(data),
    });

export const deleteWorkspace = (id: string) => requestJson<{deleted: boolean}>(
    endpoints.workspace(id), {method: 'DELETE'});

// ==================== Workspace Files ====================
export const getFileTree = (workspaceId: string) =>
    requestJson<{tree: FileTreeNode}>(endpoints.workspaceFiles(workspaceId));

export const getFileContent = (workspaceId: string, filePath: string) =>
    requestJson<{content: string; path: string}>(
        `${endpoints.workspaceFileContent(workspaceId)}?path=${
            encodeURIComponent(filePath)}`);

// ==================== Sessions ====================
export const listSessions = () =>
    requestJson<{items: SessionRecord[]}>(endpoints.sessions);

export const listSessionsByWorkspace = (workspaceId: string) =>
    requestJson<{items: SessionRecord[]}>(
        endpoints.workspaceSessions(workspaceId));

export const getSession = (id: string) =>
    requestJson<SessionRecord>(endpoints.session(id));

export const createSession = (title: string, workspaceId: string) =>
    requestJson<SessionRecord>(endpoints.sessions, {
      method: 'POST',
      body: JSON.stringify({title, workspace_id: workspaceId}),
    });

export const deleteSession = (id: string) =>
    requestJson<{deleted: boolean}>(endpoints.session(id), {method: 'DELETE'});

// ==================== Tasks ====================
export const createTask =
    (sessionId: string, workspaceId: string, input: string) =>
        requestJson<TaskRecord>(endpoints.tasks, {
          method: 'POST',
          body: JSON.stringify(
              {session_id: sessionId, workspace_id: workspaceId, input}),
        });

export const continueTask =
    (globalId: string, workspaceId: string, input: string) =>
        requestJson<TaskRecord>(endpoints.taskContinue, {
          method: 'POST',
          body: JSON.stringify(
              {global_id: globalId, workspace_id: workspaceId, input}),
        });

export const getTask = (id: string) =>
    requestJson<TaskRecord>(endpoints.task(id));

export const cancelTask = (id: string) =>
    requestJson<TaskRecord>(endpoints.cancelTask(id), {method: 'POST'});

export const deleteTask = (id: string) =>
    requestJson<{deleted: boolean}>(endpoints.task(id), {method: 'DELETE'});

export const listTasks = (sessionId: string) =>
    requestJson<{items: TaskRecord[]}>(
        `${endpoints.tasks}?session_id=${encodeURIComponent(sessionId)}`);

export const listTaskEvents = (taskId: string) =>
    requestJson<{items: TaskEventRecord[]}>(endpoints.taskEventHistory(taskId));

export const getTaskToolCalls = (taskId: string) =>
    requestJson<{items: ToolCallLog[]}>(endpoints.taskToolCalls(taskId));

export const getTaskLogs = (taskId: string) =>
    requestJson<{items: TaskLog[]}>(endpoints.taskLogs(taskId));

export const getTaskFileChanges = (taskId: string) =>
    requestJson<{items: FileChange[]}>(endpoints.taskFileChanges(taskId));

export const getTaskReplay = (taskId: string) =>
    requestJson<{events: TaskEventRecord[]}>(endpoints.taskReplay(taskId));

export const getActiveTasks = () =>
    requestJson<{items: ActiveTaskState[]}>(endpoints.activeTasks);

// ==================== Permissions ====================
export const listPendingPermissions = () =>
    requestJson<{items: PermissionRecord[]}>(endpoints.permissionsPending);

export const getPermission = (id: string) =>
    requestJson<PermissionRecord>(endpoints.permission(id));

export const approvePermission = (permissionId: string) =>
    requestJson<{id: string; status: string}>(
        endpoints.approvePermission(permissionId), {method: 'POST'});

export const rejectPermission = (permissionId: string) =>
    requestJson<{id: string; status: string}>(
        endpoints.rejectPermission(permissionId), {method: 'POST'});

// ==================== Tools ====================
export const listTools = () =>
    requestJson<{items: ToolInfo[]}>(endpoints.tools);

// ==================== Experts ====================
export const listExperts = () =>
    requestJson<{items: ExpertSummary[]}>(endpoints.experts);

export const getExpert = (name: string) =>
    requestJson<ExpertDetail>(endpoints.expert(name));

export const createExpert = (data: Partial<ExpertDetail>) =>
    requestJson<ExpertDetail>(endpoints.experts, {
      method: 'POST',
      body: JSON.stringify(data),
    });

export const updateExpert = (name: string, data: Partial<ExpertDetail>) =>
    requestJson<ExpertDetail>(endpoints.expert(name), {
      method: 'PUT',
      body: JSON.stringify(data),
    });

export const patchExpert = (name: string, data: Partial<ExpertDetail>) =>
    requestJson<ExpertDetail>(endpoints.expert(name), {
      method: 'PATCH',
      body: JSON.stringify(data),
    });

export const deleteExpert = (name: string) =>
    requestJson<{deleted: boolean}>(endpoints.expert(name), {method: 'DELETE'});

export const getExpertGraph = () =>
    requestJson<ExpertGraph>(endpoints.expertsGraph);

export const getExpertGraphPositions = () => requestJson < Record < string, {
  x: number;
  y: number
}
>> (endpoints.expertsGraphPositions);

export const saveExpertGraphPositions = (positions: Record<string, {
  x: number;
  y: number
}>) => requestJson<{saved: boolean}>(endpoints.expertsGraphPositions, {
  method: 'PUT',
  body: JSON.stringify(positions),
});

export const exportExperts = () =>
    requestJson<Record<string, ExpertDetail>>(endpoints.expertsExport);

export const importExperts = (data: Record<string, unknown>) =>
    requestJson<{imported: number}>(endpoints.expertsImport, {
      method: 'POST',
      body: JSON.stringify(data),
    });

export const validateExperts = () =>
    requestJson<{valid: boolean; errors: string[]}>(
        endpoints.expertsValidate, {method: 'POST'});

export const setExpertTools = (name: string, tools: string[]) =>
    requestJson<{expert: string; tools: string[]}>(
        endpoints.expertTools(name), {
          method: 'PUT',
          body: JSON.stringify({tools}),
        });

export const setExpertRoutes = (name: string, routes: ExpertRouteRule[]) =>
    requestJson<{expert: string}>(endpoints.expertRoutes(name), {
      method: 'PUT',
      body: JSON.stringify({routes}),
    });

export const setExpertLlm =
    (name: string, provider: string, model: string, timeout: number,
     temperature: number) =>
        requestJson<{expert: string}>(endpoints.expertLlm(name), {
          method: 'PUT',
          body: JSON.stringify({provider, model, timeout, temperature}),
        });

export const previewExpertPrompt = (name: string, goal: string) =>
    requestJson<{prompt: string; prompt_length: number}>(
        `${endpoints.expertPromptPreview(name)}`, {
          method: 'POST',
          body: JSON.stringify({goal}),
        });

export const getExpertLlmDefaults = () =>
    requestJson<Record<string, unknown>>(endpoints.expertsLlmDefaults);

export const setExpertLlmDefaults = (data: Record<string, unknown>) =>
    requestJson<Record<string, unknown>>(endpoints.expertsLlmDefaults, {
      method: 'PUT',
      body: JSON.stringify(data),
    });

// ==================== Config ====================
export const getConfig = () => requestJson<ConfigMergeView>(endpoints.config);

export const getConfigAgent = () =>
    requestJson<Record<string, unknown>>(endpoints.configAgent);

export const setConfigAgent = (data: Record<string, unknown>) =>
    requestJson<Record<string, unknown>>(endpoints.configAgent, {
      method: 'PUT',
      body: JSON.stringify(data),
    });

export const getConfigLlm = () =>
    requestJson<Record<string, unknown>>(endpoints.configLlm);

export const setConfigLlm = (data: Record<string, unknown>) =>
    requestJson<Record<string, unknown>>(endpoints.configLlm, {
      method: 'PUT',
      body: JSON.stringify(data),
    });

export const testLlmConnection = (prompt: string) =>
    requestJson<{response: string; model: string; latency_ms: number}>(
        endpoints.configLlmTest, {
          method: 'POST',
          body: JSON.stringify({prompt}),
        });

export const listLlmProviders = () =>
    requestJson<{items: LlmProvider[]}>(endpoints.configLlmProviders);

export const addLlmProvider = (data: LlmProvider) =>
    requestJson<LlmProvider>(endpoints.configLlmProviders, {
      method: 'POST',
      body: JSON.stringify(data),
    });

export const updateLlmProvider = (id: string, data: Partial<LlmProvider>) =>
    requestJson<LlmProvider>(endpoints.configLlmProvider(id), {
      method: 'PUT',
      body: JSON.stringify(data),
    });

export const deleteLlmProvider = (id: string) =>
    requestJson<{deleted: boolean}>(endpoints.configLlmProvider(id), {
      method: 'DELETE',
    });

export const getConfigLlmLocal = () =>
    requestJson<{api_key: string; api_key_masked: boolean}>(
        endpoints.configLlmLocal);

export const setConfigLlmLocal =
    (data: {api_key?: string; providers?: Record<string, {api_key: string}>}) =>
        requestJson<Record<string, unknown>>(endpoints.configLlmLocal, {
          method: 'PUT',
          body: JSON.stringify(data),
        });

export const getConfigWorkspace = () =>
    requestJson<Record<string, unknown>>(endpoints.configWorkspace);

export const setConfigWorkspace = (data: Record<string, unknown>) =>
    requestJson<Record<string, unknown>>(endpoints.configWorkspace, {
      method: 'PUT',
      body: JSON.stringify(data),
    });

export const getConfigLogging = () =>
    requestJson<Record<string, unknown>>(endpoints.configLogging);

export const setConfigLogging = (data: Record<string, unknown>) =>
    requestJson<Record<string, unknown>>(endpoints.configLogging, {
      method: 'PUT',
      body: JSON.stringify(data),
    });

export const getConfigTools = () =>
    requestJson<{items: ToolInfo[]}>(endpoints.configTools);

export const setConfigTools = (data: Record<string, unknown>) =>
    requestJson<Record<string, unknown>>(endpoints.configTools, {
      method: 'PUT',
      body: JSON.stringify(data),
    });

// ==================== Debug ====================
export const getDebugConsole = () => requestJson<
    {messages: Array<{type: string; content: string; timestamp: string}>}>(
    endpoints.debugConsole);

export const getDebugState = () =>
    requestJson<Record<string, unknown>>(endpoints.debugState);

export const getDebugEvents = () =>
    requestJson<{events: Array<Record<string, unknown>>}>(
        endpoints.debugEvents);

export const getDebugTools = () =>
    requestJson<{tools: Array<Record<string, unknown>>}>(endpoints.debugTools);

// ==================== Global ====================
export const listGlobalSessions = () =>
    requestJson<{items: SessionRecord[]}>(endpoints.globalSessions);

export const listGlobalTasks = () =>
    requestJson<{items: TaskRecord[]}>(endpoints.globalTasks);