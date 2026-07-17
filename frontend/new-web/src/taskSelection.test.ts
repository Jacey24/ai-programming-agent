import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
import type { TaskRecord } from './types';
import { latestTaskForContext, tasksForContext } from './taskSelection.ts';

const tasks: TaskRecord[] = [
  { id: 'old-failed', workspace_id: 'workspace-a', session_id: 'session-a', status: 'failed', created_at: '2026-07-17T01:00:00Z' },
  { id: 'current-completed', workspace_id: 'workspace-a', session_id: 'session-a', status: 'completed', created_at: '2026-07-17T02:00:00Z' },
  { id: 'other-session', workspace_id: 'workspace-a', session_id: 'session-b', status: 'failed', created_at: '2026-07-17T03:00:00Z' },
  { id: 'other-workspace', workspace_id: 'workspace-b', session_id: 'session-a', status: 'failed', created_at: '2026-07-17T04:00:00Z' },
];

describe('current task selection', () => {
  it('uses only tasks owned by the current workspace and session', () => {
    assert.deepEqual(tasksForContext(tasks, 'workspace-a', 'session-a').map(task => task.id), [
      'old-failed', 'current-completed',
    ]);
  });

  it('selects the latest current task instead of an older failed task', () => {
    assert.equal(latestTaskForContext(tasks, 'workspace-a', 'session-a')?.id, 'current-completed');
  });

  it('returns an empty state for a new session', () => {
    assert.equal(latestTaskForContext(tasks, 'workspace-a', 'new-session'), null);
  });
});
