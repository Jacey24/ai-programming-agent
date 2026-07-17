import type { TaskEventRecord } from './types';

export type WorkflowNodeStatus = 'idle' | 'pending' | 'running' | 'completed' | 'failed';
export type WorkflowEdgeStatus = 'idle' | 'pending' | 'running' | 'completed' | 'failed';

export interface WorkflowTaskState {
  taskId: string;
  events: TaskEventRecord[];
  nodeStates: Record<string, WorkflowNodeStatus>;
  edgeStates: Record<string, WorkflowEdgeStatus>;
  currentExpert: string | null;
  taskStatus: string | null;
}

export interface WorkflowStoreState {
  workspaceId: string | null;
  sessionId: string | null;
  activeTaskId: string | null;
  tasks: Record<string, WorkflowTaskState>;
}

export type WorkflowStoreAction =
  | { type: 'context_changed'; workspaceId: string | null; sessionId: string | null }
  | { type: 'task_selected'; taskId: string | null }
  | { type: 'events_received'; taskId: string; events: TaskEventRecord[] };

export const initialWorkflowStoreState: WorkflowStoreState = {
  workspaceId: null,
  sessionId: null,
  activeTaskId: null,
  tasks: {},
};

export function normalizeExpertName(value: unknown): string {
  return typeof value === 'string' ? value.trim().toLocaleLowerCase() : '';
}

export function workflowEdgeKey(source: string, target: string): string {
  return `${normalizeExpertName(source)}\u0000${normalizeExpertName(target)}`;
}

export function matchWorkflowNodeId(nodeIds: string[], expertName: unknown): string | null {
  const normalized = normalizeExpertName(expertName);
  if (!normalized) return null;
  return nodeIds.find(id => normalizeExpertName(id) === normalized) || null;
}

function eventMetadata(event: TaskEventRecord): Record<string, unknown> {
  return event.metadata && typeof event.metadata === 'object' ? event.metadata : {};
}

function eventKey(event: TaskEventRecord): string {
  if (event.id) return `id:${event.id}`;
  const metadata = eventMetadata(event);
  return [
    event.task_id,
    event.sequence_no,
    event.created_at,
    event.type,
    metadata.stage,
    metadata.expert,
    metadata.next,
    event.content,
  ].map(value => String(value ?? '')).join('\u001f');
}

function compareEvents(left: TaskEventRecord, right: TaskEventRecord): number {
  const leftSequence = left.sequence_no;
  const rightSequence = right.sequence_no;
  if (typeof leftSequence === 'number' && typeof rightSequence === 'number' && leftSequence !== rightSequence) {
    return leftSequence - rightSequence;
  }
  const timestampOrder = (left.created_at || '').localeCompare(right.created_at || '');
  if (timestampOrder !== 0) return timestampOrder;
  if (typeof leftSequence === 'number' && typeof rightSequence !== 'number') return -1;
  if (typeof leftSequence !== 'number' && typeof rightSequence === 'number') return 1;
  return eventKey(left).localeCompare(eventKey(right));
}

function mergeTaskEvents(taskId: string, current: TaskEventRecord[], incoming: TaskEventRecord[]): TaskEventRecord[] {
  const events = new Map<string, TaskEventRecord>();
  for (const event of [...current, ...incoming]) {
    if (event.task_id && event.task_id !== taskId) continue;
    events.set(eventKey(event), { ...event, task_id: taskId });
  }
  return [...events.values()].sort(compareEvents);
}

function setPending(
  nodeStates: Record<string, WorkflowNodeStatus>,
  expert: string,
) {
  if (!expert || expert.startsWith('_')) return;
  const current = nodeStates[expert] || 'idle';
  if (current === 'idle') nodeStates[expert] = 'pending';
}

export function reduceWorkflowEvents(
  taskId: string,
  incoming: TaskEventRecord[],
  previous?: WorkflowTaskState,
): WorkflowTaskState {
  const events = mergeTaskEvents(taskId, previous?.events || [], incoming);
  const nodeStates: Record<string, WorkflowNodeStatus> = {};
  const edgeStates: Record<string, WorkflowEdgeStatus> = {};
  let currentExpert: string | null = null;
  let runningExpert: string | null = null;
  let taskStatus: string | null = null;

  for (const event of events) {
    const type = event.type || '';
    const metadata = eventMetadata(event);
    const stage = normalizeExpertName(metadata.stage);
    const expert = normalizeExpertName(metadata.expert);
    const next = normalizeExpertName(metadata.next);

    if (type === 'agent_message' && stage === 'expert_start' && expert) {
      // A later start is explicit proof of a retry and may revive a failed node.
      nodeStates[expert] = 'running';
      currentExpert = expert;
      runningExpert = expert;
      for (const key of Object.keys(edgeStates)) {
        if (key.endsWith(`\u0000${expert}`) && edgeStates[key] === 'pending') {
          edgeStates[key] = 'running';
        }
      }
      continue;
    }

    if (type === 'agent_message' && stage === 'expert_done' && expert) {
      nodeStates[expert] = 'completed';
      currentExpert = expert;
      if (runningExpert === expert) runningExpert = null;
      for (const key of Object.keys(edgeStates)) {
        if (key.endsWith(`\u0000${expert}`) && edgeStates[key] === 'running') {
          edgeStates[key] = 'completed';
        }
      }
      if (next) {
        if (next.startsWith('_')) {
          edgeStates[workflowEdgeKey(expert, next)] = 'completed';
        } else {
          setPending(nodeStates, next);
          edgeStates[workflowEdgeKey(expert, next)] = 'pending';
        }
      }
      continue;
    }

    if (type === 'task_completed') {
      taskStatus = 'completed';
      continue;
    }

    if (type === 'task_failed' || type === 'task_cancelled') {
      const status = normalizeExpertName(metadata.status);
      taskStatus = type === 'task_cancelled' ? 'cancelled' : (status || 'failed');
      const chain = Array.isArray(metadata.expert_chain) ? metadata.expert_chain : [];
      const chainExpert = normalizeExpertName(chain[chain.length - 1]);
      const failedExpert = runningExpert || chainExpert;
      if (failedExpert && nodeStates[failedExpert] !== 'completed') {
        nodeStates[failedExpert] = 'failed';
      }
      const failureSource = failedExpert || currentExpert;
      if (failureSource) {
        for (const key of Object.keys(edgeStates)) {
          if (key.startsWith(`${failureSource}\u0000`) && edgeStates[key] === 'pending') {
            edgeStates[key] = 'failed';
          }
          if (key.endsWith(`\u0000${failureSource}`) && edgeStates[key] === 'running') {
            edgeStates[key] = 'failed';
          }
        }
      }
    }
  }

  return { taskId, events, nodeStates, edgeStates, currentExpert, taskStatus };
}

export function workflowStoreReducer(
  state: WorkflowStoreState,
  action: WorkflowStoreAction,
): WorkflowStoreState {
  if (action.type === 'context_changed') {
    if (state.workspaceId === action.workspaceId && state.sessionId === action.sessionId) return state;
    return {
      workspaceId: action.workspaceId,
      sessionId: action.sessionId,
      activeTaskId: null,
      tasks: {},
    };
  }
  if (action.type === 'task_selected') {
    return { ...state, activeTaskId: action.taskId };
  }
  const previous = state.tasks[action.taskId];
  return {
    ...state,
    tasks: {
      ...state.tasks,
      [action.taskId]: reduceWorkflowEvents(action.taskId, action.events, previous),
    },
  };
}
