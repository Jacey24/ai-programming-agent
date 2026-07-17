import type { TaskRecord } from './types';

export function tasksForContext(
  tasks: TaskRecord[],
  workspaceId: string,
  sessionId: string,
): TaskRecord[] {
  return tasks
    .filter(task => task.workspace_id === workspaceId && task.session_id === sessionId)
    .slice()
    .sort((left, right) => {
      const createdOrder = (left.created_at || '').localeCompare(right.created_at || '');
      if (createdOrder !== 0) return createdOrder;
      return left.id.localeCompare(right.id);
    });
}

export function latestTaskForContext(
  tasks: TaskRecord[],
  workspaceId: string,
  sessionId: string,
): TaskRecord | null {
  const owned = tasksForContext(tasks, workspaceId, sessionId);
  return owned[owned.length - 1] || null;
}
