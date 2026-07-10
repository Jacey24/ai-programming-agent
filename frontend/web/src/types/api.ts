export type TaskStatus = "created" | "running" | "completed" | "failed" | "cancelled" | string;

export interface ApiErrorBody {
  code?: string;
  message?: string;
}

export interface ApiEnvelope<T> {
  success: boolean;
  data: T;
  error?: ApiErrorBody;
  message?: string;
}

export interface HealthPayload {
  status?: string;
  service?: string;
  version?: string;
  database?: {
    type?: string;
    connected?: boolean;
    path?: string;
    error?: string;
  };
}

export interface SessionRecord {
  id: string;
  title?: string;
  created_at?: string;
  updated_at?: string;
}

export interface WorkspaceRecord {
  id: string;
  name?: string;
  path?: string;
  created_at?: string;
}

export interface WorkspaceFileEntry {
  name: string;
  path: string;
  type: "file" | "directory";
  size?: number;
}

export interface WorkspaceTreePayload {
  workspace_id: string;
  root: string;
  items: WorkspaceFileEntry[];
}

export interface WorkspaceFileContent {
  path: string;
  name: string;
  language: string;
  content: string;
  size: number;
  readonly: boolean;
}

export interface OpenFileTab {
  path: string;
  name: string;
  language: string;
  content: string;
  loading: boolean;
  error: string;
}

export interface TaskRecord {
  id: string;
  session_id?: string;
  workspace_id?: string;
  goal?: string;
  input?: string;
  status?: TaskStatus;
  plan?: unknown;
  current_step?: string;
  created_at?: string;
  updated_at?: string;
}

export interface TaskListPayload {
  items: TaskRecord[];
}

export interface ChatMessageRecord {
  id: string | number;
  prompt?: string;
  response?: string;
  created_at?: string;
}

export interface ChatHistoryPayload {
  items: ChatMessageRecord[];
}

export interface LogRecord {
  id?: string | number;
  task_id?: string;
  type?: string;
  content?: string;
  created_at?: string;
}

export interface ToolCallRecord {
  id?: string;
  task_id?: string;
  tool_name?: string;
  arguments?: string;
  success?: boolean;
  result?: string;
  exit_code?: number;
  created_at?: string;
}

export interface FileChangeRecord {
  id?: string;
  task_id?: string;
  file_path?: string;
  change_type?: string;
  diff?: string;
  created_at?: string;
}

export interface ReplayPayload {
  task?: TaskRecord;
  timeline?: Array<{
    type?: string;
    content?: string;
    created_at?: string;
    tool_name?: string;
    file_path?: string;
  }>;
}

export interface TaskEventRecord {
  id?: string;
  task_id?: string;
  type?: string;
  content?: string;
  metadata?: unknown;
  created_at?: string;
}

export interface TaskEventHistoryPayload {
  task_id?: string;
  items: TaskEventRecord[];
}

export interface PermissionRecord {
  id: string;
  task_id?: string;
  tool_name?: string;
  risk_level?: string;
  action?: string;
  reason?: string;
  status?: string;
  created_at?: string;
  resolved_at?: string;
}

export interface AgentStepRecord {
  id: string;
  title: string;
  description: string;
  status: "running" | "success" | "failed" | "waiting" | "idle";
  eventType: string;
  createdAt?: string;
}

export interface CreateTaskInput {
  sessionTitle: string;
  workspaceName: string;
  workspacePath: string;
  taskInput: string;
  autoRunSafeCommands: boolean;
  requireFileWritePermission: boolean;
  maxSteps: number;
  executionMode: "auto" | "answer" | "workspace";
}

export interface ListState<T> {
  status: "idle" | "loading" | "success" | "error";
  items: T[];
  error: string;
}

export interface ValueState<T> {
  status: "idle" | "loading" | "success" | "error";
  data: T | null;
  error: string;
}
