// ============================================================
// 新前端数据类型
// ============================================================

export interface ApiEnvelope<T> {
  success: boolean;
  data: T;
  error?: {code: string; message: string};
}

export interface WorkspaceRecord {
  id: string;
  name: string;
  description?: string;
  path?: string;
  permissions_config?: string | Record<string, PermissionPolicy>;
  created_at?: string;
  updated_at?: string;
}

export interface SessionRecord {
  id: string;
  title: string;
  alias?: string;
  workspace_id: string;
  created_at?: string;
  updated_at?: string;
}

export interface TaskRecord {
  id: string;
  global_id?: string;
  session_id: string;
  workspace_id: string;
  goal?: string;
  status?: string;
  current_expert?: string;
  current_stage?: string;
  expert_chain?: string[];
  created_at?: string;
  updated_at?: string;
}

export interface TaskCreationRecord extends TaskRecord {
  user_message: MessageRecord;
}

export interface TaskEventRecord {
  id: string;
  task_id?: string;
  type?: string;
  content?: string;
  metadata?: Record<string, unknown>;
  created_at?: string;
  sequence_no?: number;
}

export interface ToolInfo {
  name: string;
  description: string;
  risk_level: string;
  /** 运行时配置中的实际启用状态。 */
  enabled: boolean;
  /** 可选工具提示词。 */
  prompt?: string;
  /** 后端工具 schema 的只读参数元数据。 */
  params: Record<string, unknown>;
  /** 后端字段名是 group，API 适配层映射为 category。 */
  category: string;
}

export interface HealthResponse {
  status: string;
  service?: string;
  version?: string;
  database?: {type: string; connected: boolean; path: string;};
  llm?: {provider: string; model: string;};
}

export interface ActiveTaskState {
  task_id: string;
  global_id: string;
  workspace_id: string;
  goal: string;
  current_expert: string;
  current_stage: string;
  expert_chain: string[];
  status: string;
}

export interface PermissionRecord {
  id: string;
  task_id?: string;
  tool_name?: string;
  risk_level?: string;
  status?: string;
  created_at?: string;
}

export interface ToolCallLog {
  id: string;
  task_id?: string;
  tool_name?: string;
  arguments?: string;
  result?: string;
  success?: boolean;
  exit_code?: number;
  created_at?: string;
}

export interface TaskLog {
  id: string;
  task_id?: string;
  level?: string;
  message?: string;
  created_at?: string;
}

export interface FileChange {
  id: string;
  task_id?: string;
  path?: string;
  type?: string;
  created_at?: string;
}

// Experts
export interface ExpertSummary {
  name: string;
  description: string;
  is_entry: boolean;
  context_isolation: boolean;
}

export interface Exponent extends ExpertSummary {
  context_template: string;
  visible_tools: string[];
  can_modify_plan: boolean;
  can_write_summary: boolean;
  read_global_actively: boolean;
  llm_provider: string;
  llm_model: string;
  llm_timeout: number;
  llm_temperature: number;
  max_internal_rounds: number;
  tool_timeout_seconds: number;
  next_rules: ExpertRouteRule[];
  on_fail: string;
}

export interface ExpertRouteRule {
  type: string;
  value: string;
  route_to: string;
  priority: number;
}

export interface ExpertNode {
  id: string;
  label: string;
  description: string;
  is_entry: boolean;
  is_exit: boolean;
  context_isolation: boolean;
  visible_tools: string[];
  permissions: {
    can_modify_plan: boolean; can_write_summary: boolean;
    read_global_actively: boolean;
  };
  llm: {provider: string; model: string; temperature: number; timeout: number;};
  limits: {max_internal_rounds: number; tool_timeout_seconds: number;};
  on_fail: string;
  position?: {x: number; y: number};
}

export interface ExpertEdge {
  id: string;
  source: string;
  target: string;
  condition_type: string;
  condition_value: string;
  priority: number;
  label: string;
}

export interface ExpertVirtualNode {
  id: string;
  label: string;
  type: string;
}

export interface ExpertGraph {
  nodes: ExpertNode[];
  edges: ExpertEdge[];
  virtual_nodes: ExpertVirtualNode[];
}

// Config
export interface ConfigMergeView {
  agent: Record<string, unknown>;
  llm: Record<string, unknown>;
  workspace: Record<string, unknown>;
  logging: Record<string, unknown>;
  tools: Record<string, unknown>;
}

export interface LlmProvider {
  id: string;
  base_url: string;
  model: string;
  api_key_env?: string;
  api_key?: string;
  api_key_masked?: boolean;
  set_default?: boolean;
}

export type PermissionPolicy = 'ask' | 'auto_approve' | 'deny';

// Debug
export interface DebugConsoleMessage {
  type: string;
  content: string;
  timestamp: string;
}

// File tree
export interface FileTreeNode {
  name: string;
  path: string;
  type: 'file'|'directory';
  size: number;
  children?: FileTreeNode[];
}

export interface WorkspaceFileContent {
  path: string;
  name: string;
  language: string;
  content: string;
  size: number;
  readonly: boolean;
  encoding: 'utf-8'|'utf-8-bom'|'system';
}

export type PillTone = 'ok'|'warning'|'danger'|'idle';

export interface PillStatus {
  label: string;
  value: string;
  detail: string;
  tone: PillTone;
}

// 前端 UI 状态
export type AppPhase = 'workspace_select'|'normal';
export type GlassTab = 'chat'|'files';
export type ThemeMode = 'dark'|'light';

export interface AppState {
  phase: AppPhase;
  theme: ThemeMode;
  activeWorkspace: WorkspaceRecord|null;
  activeSession: SessionRecord|null;
  activeTab: GlassTab;
}

export interface MessageRecord {
  id: string;
  session_id: string;
  task_id: string | null;
  role: 'user' | 'assistant' | 'system';
  message_type: 'normal' | 'result' | 'error';
  content: string;
  sequence_no: number;
  source_event_id: string | null;
  created_at: string;
}
