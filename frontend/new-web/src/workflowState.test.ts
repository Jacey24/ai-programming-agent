import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
import type { TaskEventRecord } from './types';
import {
  initialWorkflowStoreState,
  matchWorkflowNodeId,
  reduceWorkflowEvents,
  workflowEdgeKey,
  workflowStoreReducer,
} from './workflowState.ts';

function event(
  id: string,
  sequence: number,
  stage?: string,
  expert?: string,
  next?: string,
  type = 'agent_message',
  taskId = 'task-a',
): TaskEventRecord {
  return {
    id,
    task_id: taskId,
    type,
    sequence_no: sequence,
    created_at: `2026-07-17T00:00:${String(sequence).padStart(2, '0')}Z`,
    metadata: { stage, expert, next },
  };
}

describe('workflow event reduction', () => {
  it('maps start, done, next and edge progress', () => {
    const state = reduceWorkflowEvents('task-a', [
      event('1', 1, 'expert_start', ' Planner '),
      event('2', 2, 'expert_done', 'planner', 'EXECUTOR'),
      event('3', 3, 'expert_start', 'executor'),
    ]);
    assert.equal(state.nodeStates.planner, 'completed');
    assert.equal(state.nodeStates.executor, 'running');
    assert.equal(state.edgeStates[workflowEdgeKey('planner', 'executor')], 'running');
  });

  it('keeps next pending until it starts', () => {
    const state = reduceWorkflowEvents('task-a', [event('1', 1, 'expert_done', 'planner', 'executor')]);
    assert.equal(state.nodeStates.executor, 'pending');
    assert.equal(state.edgeStates[workflowEdgeKey('planner', 'executor')], 'pending');
  });

  it('fails only the running expert and preserves completed predecessors', () => {
    const state = reduceWorkflowEvents('task-a', [
      event('1', 1, 'expert_start', 'planner'),
      event('2', 2, 'expert_done', 'planner', 'executor'),
      event('3', 3, 'expert_start', 'executor'),
      event('4', 4, undefined, undefined, undefined, 'task_failed'),
    ]);
    assert.equal(state.nodeStates.planner, 'completed');
    assert.equal(state.nodeStates.executor, 'failed');
  });

  it('preserves the completed path when the task completes', () => {
    const state = reduceWorkflowEvents('task-a', [
      event('1', 1, 'expert_done', 'planner', 'executor'),
      event('2', 2, 'expert_done', 'executor', '_done'),
      event('3', 3, undefined, undefined, undefined, 'task_completed'),
    ]);
    assert.equal(state.nodeStates.planner, 'completed');
    assert.equal(state.nodeStates.executor, 'completed');
    assert.equal(state.taskStatus, 'completed');
  });

  it('isolates tasks and sessions explicitly', () => {
    let store = workflowStoreReducer(initialWorkflowStoreState, {
      type: 'context_changed', workspaceId: 'workspace-a', sessionId: 'session-a',
    });
    store = workflowStoreReducer(store, { type: 'events_received', taskId: 'task-a', events: [event('1', 1, 'expert_start', 'planner')] });
    store = workflowStoreReducer(store, { type: 'task_selected', taskId: 'task-b' });
    assert.equal(store.tasks['task-a'].nodeStates.planner, 'running');
    assert.equal(store.tasks['task-b'], undefined);

    store = workflowStoreReducer(store, {
      type: 'context_changed', workspaceId: 'workspace-a', sessionId: 'session-b',
    });
    assert.deepEqual(store.tasks, {});
    assert.equal(store.activeTaskId, null);
  });

  it('restores deterministically, ignores duplicates and resists older late events', () => {
    const done = event('done', 2, 'expert_done', 'planner', 'executor');
    const oldStart = event('start', 1, 'expert_start', 'planner');
    const state = reduceWorkflowEvents('task-a', [done, oldStart, done]);
    assert.equal(state.events.length, 2);
    assert.equal(state.nodeStates.planner, 'completed');
    assert.deepEqual(reduceWorkflowEvents('task-a', [], state), state);
  });

  it('allows a failed expert to run again only after a newer start', () => {
    const failed = reduceWorkflowEvents('task-a', [
      event('1', 1, 'expert_start', 'reviewer'),
      event('2', 2, undefined, undefined, undefined, 'task_failed'),
    ]);
    assert.equal(failed.nodeStates.reviewer, 'failed');
    const retried = reduceWorkflowEvents('task-a', [event('3', 3, 'expert_start', 'reviewer')], failed);
    assert.equal(retried.nodeStates.reviewer, 'running');
  });

  it('matches core and custom expert IDs without mis-highlighting unknown names', () => {
    const ids = ['planner', 'executor', 'reviewer', 'summarizer', 'Custom-Expert'];
    assert.equal(matchWorkflowNodeId(ids, '  PLANNER '), 'planner');
    assert.equal(matchWorkflowNodeId(ids, 'custom-expert'), 'Custom-Expert');
    assert.equal(matchWorkflowNodeId(ids, 'missing'), null);
  });

  it('marks an unfinished transition failed without downgrading its completed source', () => {
    const state = reduceWorkflowEvents('task-a', [
      event('1', 1, 'expert_done', 'planner', 'missing-expert'),
      { ...event('2', 2, undefined, undefined, undefined, 'task_failed'), metadata: { expert_chain: ['planner'] } },
    ]);
    assert.equal(state.nodeStates.planner, 'completed');
    assert.equal(state.edgeStates[workflowEdgeKey('planner', 'missing-expert')], 'failed');
  });
});
