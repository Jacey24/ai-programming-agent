import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import ReactMarkdown from 'react-markdown';
import type { MessageRecord, SessionRecord, TaskEventRecord, TaskRecord, ToolCallLog } from '../types';
import { createTask, cancelTask, getTaskToolCalls, listMessages, listTaskEvents, listTasks, approvePermission, rejectPermission } from '../api/api';
import { ToolCallCard } from './ToolCallCard';
import { latestTaskForContext, tasksForContext } from '../taskSelection';

interface Props {
  workspaceId: string;
  session: SessionRecord;
  onBack: () => void;
  onWorkflowTaskSelected: (task: TaskRecord | null) => void;
  onWorkflowEvents: (task: TaskRecord, events: TaskEventRecord[]) => void;
}

type TimelineItem =
  | { kind: 'msg'; data: MessageRecord; time: string }
  | { kind: 'tool'; data: { toolName: string; status: 'running' | 'success' | 'failed'; arguments?: unknown; output?: string; id: string }; time: string }
  | { kind: 'status'; data: { title: string; detail?: string; status: 'running' | 'success' | 'failed'; id: string }; time: string }
  | { kind: 'perm'; data: { permissionId: string; toolName: string; detail: string; id: string }; time: string }
  | { kind: 'streaming'; data: { taskId: string; content: string }; time: string }
  | { kind: 'loading' }
  | { kind: 'dot-loader' };

function objMeta(meta: unknown): Record<string, unknown> {
  return meta && typeof meta === 'object' ? meta as Record<string, unknown> : {};
}

function strMeta(meta: Record<string, unknown>, key: string): string {
  const v = meta[key];
  if (typeof v === 'string') return v;
  if (typeof v === 'boolean' || typeof v === 'number') return String(v);
  return '';
}

// ★ 过滤 LLM 输出中的工具调用语法（如 "file.list {\"path\":\".\",\"depth\":1}"）
// 匹配模式: 工具名(可含.号) 空格 {JSON参数}
const TOOL_CALL_RE = /^[a-z_.]+\s+\{.*\}$/;

function stripToolCalls(content: string): string {
  return content
    .split('\n')
    .filter(line => !TOOL_CALL_RE.test(line.trim()))
    .join('\n')
    .replace(/\n{3,}/g, '\n\n') // 压缩多行空行
    .trim();
}

const TOOL_EVENT_TYPES = ['tool_started', 'tool_output', 'tool_finished'];
const PERM_EVENT_TYPES = ['permission_required', 'permission_resolved'];

function dedupeToolEvents(events: TaskEventRecord[]): TaskEventRecord[] {
  const latest = new Map<string, number>();
  const allTypes = [...TOOL_EVENT_TYPES, ...PERM_EVENT_TYPES];

  events.forEach((evt, idx) => {
    const type = evt.type || '';
    if (!allTypes.includes(type)) return;
    const m = objMeta(evt.metadata);

    if (TOOL_EVENT_TYPES.includes(type)) {
      const key = strMeta(m, 'tool_call_id') || strMeta(m, 'tool_name') || `tool_${idx}`;
      latest.set(key, idx);
    } else if (PERM_EVENT_TYPES.includes(type)) {
      const key = strMeta(m, 'permission_id') || strMeta(m, 'request_id') || strMeta(m, 'tool_name') || `perm_${idx}`;
      latest.set(key, idx);
    }
  });

  return events.filter((evt, idx) => {
    const type = evt.type || '';
    if (!allTypes.includes(type)) return true;
    const m = objMeta(evt.metadata);

    if (TOOL_EVENT_TYPES.includes(type)) {
      const key = strMeta(m, 'tool_call_id') || strMeta(m, 'tool_name') || `tool_${idx}`;
      return latest.get(key) === idx;
    } else if (PERM_EVENT_TYPES.includes(type)) {
      const key = strMeta(m, 'permission_id') || strMeta(m, 'request_id') || strMeta(m, 'tool_name') || `perm_${idx}`;
      return latest.get(key) === idx;
    }
    return true;
  });
}

// 所有需要从持久化记录回放的事件类型
const PERSISTED_EVENT_TYPES = [
  'agent_message',
  'tool_started', 'tool_output', 'tool_finished',
  'task_planning', 'permission_required', 'permission_resolved',
  'file_changed', 'task_completed', 'task_failed', 'task_cancelled',
];

const TERMINAL_TASK_STATUSES = new Set(['completed', 'failed', 'cancelled', 'interrupted']);
const TERMINAL_EVENT_TYPES = new Set(['task_completed', 'task_failed', 'task_cancelled']);

function isTerminalTask(task: TaskRecord | null): boolean {
  return Boolean(task?.status && TERMINAL_TASK_STATUSES.has(task.status));
}

function isInterruptedEvent(event: TaskEventRecord): boolean {
  return event.type === 'task_failed' && strMeta(objMeta(event.metadata), 'status') === 'interrupted';
}

export function ChatView({ workspaceId, session, onBack, onWorkflowTaskSelected, onWorkflowEvents }: Props) {
  const [messages, setMessages] = useState<MessageRecord[]>([]);
  const [input, setInput] = useState('');
  const [activeTask, setActiveTask] = useState<TaskRecord | null>(null);
  const [streaming, setStreaming] = useState(false);
  const [loadingHistory, setLoadingHistory] = useState(true);
  const eventSource = useRef<EventSource | null>(null);
  const activeSessionId = useRef(session.id);
  activeSessionId.current = session.id;
  const seenIds = useRef<Set<string>>(new Set());
  const scrollRef = useRef<HTMLDivElement>(null);
  const historyRequest = useRef(0);
  const [rawEvents, setRawEvents] = useState<TaskEventRecord[]>([]);
  const [toolCalls, setToolCalls] = useState<ToolCallLog[]>([]);

  const [streamingContent, setStreamingContent] = useState<Record<string, string>>({});
  const streamingRef = useRef<Record<string, string>>({});

  const mergeMessages = useCallback((incoming: MessageRecord[]) => {
    setMessages(prev => {
      const byId = new Map(prev.map(message => [message.id, message]));
      for (const message of incoming) byId.set(message.id, message);
      return [...byId.values()].sort((a, b) => a.sequence_no - b.sequence_no);
    });
  }, []);

  const appendEvents = useCallback((evts: TaskEventRecord[]) => {
    setRawEvents(prev => {
      const existing = new Set(prev.map(e => e.id));
      const next = [...prev];
      for (const e of evts) {
        if (TERMINAL_EVENT_TYPES.has(e.type || '') &&
            next.some(existing => existing.task_id === e.task_id &&
              TERMINAL_EVENT_TYPES.has(existing.type || ''))) continue;
        if (e.id && existing.has(e.id)) continue;
        if (e.id) existing.add(e.id);
        next.push(e);
      }
      return next.sort((a, b) => {
        if (a.task_id && a.task_id === b.task_id) {
          return (a.sequence_no || 0) - (b.sequence_no || 0);
        }
        return (a.created_at || '').localeCompare(b.created_at || '');
      });
    });
  }, []);

  const closeStream = useCallback(() => {
    eventSource.current?.close();
    eventSource.current = null;
    setStreaming(false);
    streamingRef.current = {};
    setStreamingContent({});
  }, []);

  const startSSE = useCallback((task: TaskRecord) => {
    if (task.session_id !== session.id || task.workspace_id !== workspaceId) return;
    closeStream();
    const seenStorageKey = `sse_seen_${task.id}`;
    try {
      const stored = JSON.parse(sessionStorage.getItem(seenStorageKey) || '[]');
      seenIds.current = new Set(Array.isArray(stored) ? stored.filter(value => typeof value === 'string') : []);
    } catch {
      seenIds.current.clear();
    }
    streamingRef.current = {};
    setStreamingContent({});
    setStreaming(true);
    const source = new EventSource(
      `/api/v1/tasks/${encodeURIComponent(task.id)}/events?session_id=${encodeURIComponent(session.id)}`,
    );
    eventSource.current = source;

    const handler = (e: MessageEvent) => {
      try {
        if (activeSessionId.current !== session.id) return;
        const evt: TaskEventRecord = JSON.parse(e.data);
        if (evt.type !== 'stream_end' && evt.task_id !== task.id) return;
        const evtId = evt.id || e.lastEventId || `${task.id}:${evt.type}:${evt.sequence_no ?? evt.created_at}`;
        if (seenIds.current.has(evtId)) return;
        seenIds.current.add(evtId);
        try {
          sessionStorage.setItem(seenStorageKey, JSON.stringify([...seenIds.current].slice(-500)));
        } catch {}

        onWorkflowEvents(task, [evt]);

        if (evt.type === 'agent_message_chunk' && evt.content) {
          const content = (streamingRef.current[task.id] || '') + evt.content;
          streamingRef.current = { ...streamingRef.current, [task.id]: content };
          setStreamingContent(prev => ({ ...prev, [task.id]: content }));
          return;
        }

        if (PERSISTED_EVENT_TYPES.includes(evt.type || '') || evt.type === 'stream_end') {
          appendEvents([evt]);
        }
        const terminalStatus = evt.type === 'task_completed' ? 'completed'
          : evt.type === 'task_cancelled' ? 'cancelled'
          : evt.type === 'task_failed'
            ? (strMeta(objMeta(evt.metadata), 'status') === 'interrupted' ? 'interrupted' : 'failed')
            : null;
        if (terminalStatus) {
          setActiveTask(prev => prev?.id === task.id ? { ...prev, status: terminalStatus } : prev);
          const alignFinalMessage = async () => {
            try {
              const [persisted, persistedTools] = await Promise.all([
                listMessages(session.id),
                getTaskToolCalls(task.id),
              ]);
              if (activeSessionId.current !== session.id) return;
              mergeMessages((persisted.items || []).filter(message => message.session_id === session.id));
              setToolCalls(prev => {
                const byId = new Map(prev.map(call => [call.id, call]));
                for (const call of persistedTools.items || []) byId.set(call.id, call);
                return [...byId.values()];
              });
            } catch {}
            if (activeSessionId.current !== session.id) return;
            setStreamingContent(prev => {
              const next = { ...prev };
              delete next[task.id];
              return next;
            });
            delete streamingRef.current[task.id];
            closeStream();
          };
          void alignFinalMessage();
        }
      } catch {}
    };

    source.addEventListener('agent_message', handler);
    source.addEventListener('agent_message_chunk', handler);
    source.addEventListener('tool_started', handler);
    source.addEventListener('tool_output', handler);
    source.addEventListener('tool_finished', handler);
    source.addEventListener('task_planning', handler);
    source.addEventListener('permission_required', handler);
    source.addEventListener('permission_resolved', handler);
    source.addEventListener('file_changed', handler);
    source.addEventListener('task_completed', handler);
    source.addEventListener('task_failed', handler);
    source.addEventListener('task_cancelled', handler);
    source.addEventListener('stream_end', handler);
    source.onerror = () => {
      if (source.readyState === EventSource.CLOSED) setStreaming(false);
    };
  }, [appendEvents, closeStream, mergeMessages, onWorkflowEvents, session.id, workspaceId]);

  useEffect(() => {
    const requestId = ++historyRequest.current;
    const controller = new AbortController();
    closeStream();
    streamingRef.current = {};
    setStreamingContent({});
    const loadHistory = async () => {
      setLoadingHistory(true);
      setMessages([]);
      setRawEvents([]);
      setToolCalls([]);
      setActiveTask(null);
      onWorkflowTaskSelected(null);
      try {
        const [messageRes, taskRes] = await Promise.all([
          listMessages(session.id, controller.signal),
          listTasks(session.id, controller.signal),
        ]);
        if (requestId !== historyRequest.current || controller.signal.aborted) return;
        setMessages((messageRes.items || []).slice().sort((a, b) => a.sequence_no - b.sequence_no));
        const tasks = tasksForContext(taskRes.items || [], workspaceId, session.id);
        const latestTask = latestTaskForContext(tasks, workspaceId, session.id);
        setActiveTask(latestTask);
        onWorkflowTaskSelected(latestTask);
        const historyEvents: TaskEventRecord[] = [];
        const historyToolCalls: ToolCallLog[] = [];
        for (const t of tasks) {
          const isDisplayTask = t.id === latestTask?.id;
          let hasInterruptedEvent = false;
          try {
            const [evtRes, toolRes] = await Promise.all([
              listTaskEvents(t.id, controller.signal),
              getTaskToolCalls(t.id, controller.signal),
            ]);
            if (isDisplayTask) {
              historyToolCalls.push(...(toolRes.items || []).filter(call => call.task_id === t.id));
            }
            onWorkflowEvents(t, evtRes.items || []);
            for (const evt of evtRes.items || []) {
              // ★ 工具/权限/状态事件走 appendEvents（关闭后回来也能看到）
              if (isDisplayTask && PERSISTED_EVENT_TYPES.includes(evt.type || '') &&
                  !TOOL_EVENT_TYPES.includes(evt.type || '')) {
                const historyEvent = t.status === 'interrupted' && evt.type === 'task_failed'
                  ? { ...evt, metadata: { ...objMeta(evt.metadata), status: 'interrupted' } }
                  : evt;
                hasInterruptedEvent ||= isInterruptedEvent(historyEvent);
                historyEvents.push(historyEvent);
              }
            }
          } catch {}
          if (isDisplayTask && t.status === 'interrupted' && !hasInterruptedEvent) {
            historyEvents.push({
              id: `interrupted_${t.id}`,
              task_id: t.id,
              type: 'task_failed',
              content: '任务因后端异常退出而中断',
              metadata: { status: 'interrupted' },
              created_at: t.updated_at || t.created_at,
            });
          }
        }
        if (requestId === historyRequest.current && historyEvents.length > 0) {
          appendEvents(historyEvents);
        }
        if (requestId === historyRequest.current) {
          setToolCalls(historyToolCalls);
          if (latestTask && !isTerminalTask(latestTask)) startSSE(latestTask);
        }
      } catch {}
      if (requestId === historyRequest.current) setLoadingHistory(false);
    };
    void loadHistory();
    return () => {
      controller.abort();
      ++historyRequest.current;
    };
  }, [session.id, workspaceId, appendEvents, closeStream, onWorkflowEvents, onWorkflowTaskSelected, startSSE]);

  useEffect(() => () => closeStream(), [closeStream]);

  useEffect(() => {
    if (loadingHistory) return;
    const el = scrollRef.current;
    if (el) {
      el.scrollTo({ top: el.scrollHeight, behavior: 'instant' as ScrollBehavior });
    }
  }, [messages, rawEvents, loadingHistory, streamingContent]);

  const handleSubmit = async () => {
    const text = input.trim();
    if (!text || streaming) return;
    setInput('');
    const userMsgId = `temp_user_${Date.now()}`;
    const temporaryMessage: MessageRecord = {
      id: userMsgId,
      session_id: session.id,
      task_id: null,
      role: 'user',
      message_type: 'normal',
      content: text,
      sequence_no: Number.MAX_SAFE_INTEGER,
      source_event_id: null,
      created_at: new Date().toISOString(),
    };
    mergeMessages([temporaryMessage]);
    try {
      const task = await createTask(session.id, workspaceId, text);
      if (task.session_id !== session.id || task.workspace_id !== workspaceId) {
        throw new Error('Task session mismatch');
      }
      setActiveTask(task);
      onWorkflowTaskSelected(task);
      setRawEvents([]);
      setToolCalls([]);
      seenIds.current.clear();
      setMessages(prev => prev
        .filter(message => message.id !== userMsgId && message.id !== task.user_message.id)
        .concat(task.user_message)
        .sort((a, b) => a.sequence_no - b.sequence_no));
      startSSE(task);
    } catch {
      setMessages(prev => prev.filter(message => message.id !== userMsgId));
    }
  };

  const handleCancel = async () => {
    if (!activeTask || isTerminalTask(activeTask)) return;
    try {
      const updated = await cancelTask(activeTask.id);
      if (updated.session_id === session.id) setActiveTask(updated);
    } catch {}
  };

  const [resolvedPerms, setResolvedPerms] = useState<Set<string>>(new Set());

  const handleApprove = useCallback(async (permId: string) => {
    if (!permId) return;
    try {
      await approvePermission(permId);
      setResolvedPerms(prev => new Set(prev).add(permId));
    } catch {}
  }, []);

  const handleReject = useCallback(async (permId: string) => {
    if (!permId) return;
    try {
      await rejectPermission(permId);
      setResolvedPerms(prev => new Set(prev).add(permId));
    } catch {}
  }, []);

  const unifiedTimeline: TimelineItem[] = useMemo(() => {
    if (loadingHistory) {
      return [{ kind: 'loading' }];
    }

    const items: TimelineItem[] = [];

    for (const msg of messages) {
      items.push({ kind: 'msg', data: msg, time: msg.created_at });
    }

    for (const call of toolCalls) {
      items.push({
        kind: 'tool',
        data: {
          toolName: call.tool_name || 'tool',
          status: call.success ? 'success' : 'failed',
          arguments: call.arguments,
          output: call.result,
          id: call.id,
        },
        time: call.created_at || '',
      });
    }

    const deduped = dedupeToolEvents(rawEvents);
    for (const evt of deduped) {
      const type = evt.type || '';
      const meta = objMeta(evt.metadata);
      const id = evt.id || `${type}:${evt.created_at}`;
      const time = evt.created_at || '';

      if (type === 'tool_started' || type === 'tool_output' || type === 'tool_finished') {
        const persistedToolId = strMeta(meta, 'tool_call_id');
        if (persistedToolId && toolCalls.some(call => call.id === persistedToolId)) continue;
        const toolName = strMeta(meta, 'tool_name') || 'tool';
        let status: 'running' | 'success' | 'failed' = 'running';
        if (type === 'tool_finished') {
          status = strMeta(meta, 'success') === 'false' || meta.success === false ? 'failed' : 'success';
        }
        items.push({ kind: 'tool', data: { toolName, status, arguments: meta.arguments, output: evt.content || undefined, id }, time });
      } else if (type === 'task_planning') {
        items.push({ kind: 'status', data: { title: 'Planning', detail: evt.content || 'Agent is planning the task.', status: 'success', id }, time });
      } else if (type === 'permission_required') {
        const permId = strMeta(meta, 'permission_id') || strMeta(meta, 'request_id') || '';
        const toolName = strMeta(meta, 'tool_name') || 'tool';
        items.push({ kind: 'perm', data: { permissionId: permId, toolName, detail: evt.content || `需要权限确认: ${toolName}`, id }, time });
      } else if (type === 'permission_resolved') {
        items.push({ kind: 'status', data: { title: 'Permission resolved', detail: evt.content || '权限已处理', status: 'success', id }, time });
      } else if (type === 'task_completed') {
        items.push({ kind: 'status', data: { title: '✓ 任务完成', status: 'success', id }, time });
      } else if (type === 'task_failed') {
        const interrupted = strMeta(meta, 'status') === 'interrupted';
        items.push({ kind: 'status', data: { title: interrupted ? '任务中断' : '任务失败', detail: interrupted ? evt.content : undefined, status: 'failed', id }, time });
      } else if (type === 'task_cancelled') {
        items.push({ kind: 'status', data: { title: evt.content || '任务已取消', status: 'failed', id }, time });
      } else if (type === 'stream_end') {
        items.push({ kind: 'status', data: { title: 'Stream ended', detail: evt.content || '', status: 'success', id }, time });
      }
    }

    for (const [taskId, content] of Object.entries(streamingContent)) {
      if (content) items.push({ kind: 'streaming', data: { taskId, content }, time: `z:${taskId}` });
    }

    items.sort((a, b) => {
      if ('time' in a && 'time' in b) {
        return a.time.localeCompare(b.time);
      }
      return 0;
    });

    if (streaming && items.length === 0) {
      items.push({ kind: 'dot-loader' });
    }

    return items;
  }, [messages, rawEvents, toolCalls, loadingHistory, streaming, streamingContent]);

  const TOP_HEIGHT = 54;

  return (
    <div className="flex flex-col h-full">
      <div className="flex items-center shrink-0 border-b border-[var(--glass-border-strong)]" style={{ height: TOP_HEIGHT, padding: '0 20px', gap: 8 }}>
        <button onClick={onBack} className="btn-secondary" style={{ minWidth: 56, height: 30, fontSize: 12, display: 'flex', alignItems: 'center', justifyContent: 'center', padding: '0 12px' }}>
          ← 返回
        </button>
        <div className="min-w-0 flex-1">
          <div className="text-[9px] text-[var(--text-secondary)] truncate" title={`${workspaceId} / ${session.id}`}>
            Workspace {workspaceId} / Session {session.title || session.id}
          </div>
          <div className="text-xs text-[var(--text-primary)] truncate" title={activeTask?.id || 'No task'}>
            Task {activeTask?.id || '尚未创建'}
          </div>
        </div>
        {activeTask?.status && (
          <span className="text-[9px] uppercase rounded-md shrink-0" style={{ padding: '3px 6px', color: 'var(--accent-lighter)', border: '1px solid var(--glass-border-strong)', background: 'var(--surface)' }}>
            {activeTask.status}
          </span>
        )}
        {streaming && <span className="w-2 h-2 rounded-full bg-[var(--accent-light)] animate-pulse" />}
      </div>

      <div ref={scrollRef} className="flex-1 overflow-y-auto" style={{ padding: '16px 20px', display: 'flex', flexDirection: 'column', gap: 12 }}>
        {loadingHistory && (
          <div className="text-xs text-[var(--text-secondary)] text-center" style={{ padding: '16px 0' }}>加载历史记录...</div>
        )}

        {unifiedTimeline.map(item => {
          if (item.kind === 'loading') return null;

          if (item.kind === 'msg') {
            const msg = item.data;
            return (
              <div key={msg.id} className={`flex anim-slide-up ${msg.role === 'user' ? 'justify-end' : 'justify-start'}`}>
                <div
                  className="max-w-[85%] rounded-xl text-xs leading-relaxed"
                  style={{
                    padding: '8px 12px',
                    background: msg.role === 'user' ? 'var(--bubble-user)' : 'var(--bubble-ai)',
                    border: msg.role === 'user' ? 'none' : '1px solid var(--glass-border)',
                    color: msg.role === 'user' ? '#fff' : 'var(--text-primary)',
                  }}
                >
                  <div className="markdown-body">
                    <ReactMarkdown>{stripToolCalls(msg.content)}</ReactMarkdown>
                  </div>
                </div>
              </div>
            );
          }

          if (item.kind === 'streaming') {
            return (
              <div key={`streaming-bubble-${item.data.taskId}`} className="flex justify-start">
                <div
                  className="max-w-[85%] rounded-xl text-xs leading-relaxed"
                  style={{
                    padding: '8px 12px',
                    background: 'var(--bubble-ai)',
                    border: '1px solid var(--glass-border)',
                    color: 'var(--text-primary)',
                  }}
                >
                  <div className="markdown-body">
                    <ReactMarkdown>{item.data.content}</ReactMarkdown>
                    <span style={{
                      display: 'inline-block',
                      width: 1,
                      height: 14,
                      background: 'var(--accent-light)',
                      marginLeft: 1,
                      verticalAlign: 'text-bottom',
                      animation: 'blink 1s step-end infinite',
                    }} />
                  </div>
                </div>
              </div>
            );
          }

          if (item.kind === 'dot-loader') {
            return (
              <div key="dot-loader" className="flex justify-start">
                <div className="rounded-xl bg-[var(--bubble-ai)] border border-[var(--glass-border)]" style={{ padding: '8px 12px', display: 'inline-flex', gap: 4 }}>
                  <span style={{ width: 6, height: 6, borderRadius: '50%', background: 'var(--accent-light)', animation: 'bounce 0.6s infinite', animationDelay: '0ms' }} />
                  <span style={{ width: 6, height: 6, borderRadius: '50%', background: 'var(--accent-light)', animation: 'bounce 0.6s infinite', animationDelay: '150ms' }} />
                  <span style={{ width: 6, height: 6, borderRadius: '50%', background: 'var(--accent-light)', animation: 'bounce 0.6s infinite', animationDelay: '300ms' }} />
                </div>
              </div>
            );
          }

          if (item.kind === 'tool') {
            return <ToolCallCard key={item.data.id} {...item.data} />;
          }

          if (item.kind === 'perm') {
            const isResolved = resolvedPerms.has(item.data.permissionId);
            return (
              <div key={item.data.id} className="flex justify-start">
                <div className="rounded-xl border flex flex-col gap-2" style={{
                  padding: '10px 14px',
                  maxWidth: '85%',
                  background: 'rgba(59,130,246,0.08)',
                  borderColor: 'rgba(59,130,246,0.35)',
                }}>
                  <div className="flex items-start gap-2">
                    <span style={{
                      width: 8, height: 8, borderRadius: '50%',
                      background: '#3b82f6', flexShrink: 0, marginTop: 2,
                      animation: 'pulse 1.5s infinite',
                    }} />
                    <div className="flex-1">
                      <div className="text-xs font-bold text-[var(--text-primary)]">需要权限确认</div>
                      <div className="text-[10px] text-[var(--text-secondary)] mt-0.5">{item.data.detail}</div>
                      <div className="text-[10px] text-[var(--accent-lighter)] mt-0.5">工具: {item.data.toolName}</div>
                    </div>
                  </div>
                  {!isResolved && item.data.permissionId && (
                    <div className="flex gap-2" style={{ paddingLeft: 16 }}>
                      <button
                        onClick={() => handleApprove(item.data.permissionId)}
                        className="btn-primary"
                        style={{ height: 26, fontSize: 11, padding: '0 14px', minWidth: 52, borderRadius: 6 }}
                      >
                        批准
                      </button>
                      <button
                        onClick={() => handleReject(item.data.permissionId)}
                        className="btn-danger"
                        style={{ height: 26, fontSize: 11, padding: '0 14px', minWidth: 52, borderRadius: 6 }}
                      >
                        拒绝
                      </button>
                    </div>
                  )}
                  {isResolved && (
                    <div className="text-[10px] text-[var(--accent-lighter)]" style={{ paddingLeft: 16 }}>已处理</div>
                  )}
                </div>
              </div>
            );
          }

          if (item.kind === 'status') {
            return (
              <div key={item.data.id} className="flex justify-start">
                <div className="rounded-xl border flex items-start gap-2" style={{
                  padding: '8px 12px',
                  background: item.data.status === 'running' ? 'rgba(59,130,246,0.1)' : item.data.status === 'failed' ? 'rgba(239,68,68,0.1)' : 'rgba(34,197,94,0.1)',
                  borderColor: item.data.status === 'running' ? 'rgba(59,130,246,0.3)' : item.data.status === 'failed' ? 'rgba(239,68,68,0.3)' : 'rgba(34,197,94,0.3)',
                }}>
                  <span style={{
                    width: 8, height: 8, borderRadius: '50%',
                    background: item.data.status === 'running' ? '#3b82f6' : item.data.status === 'failed' ? '#ef4444' : '#22c55e',
                    flexShrink: 0, marginTop: 2,
                    animation: item.data.status === 'running' ? 'pulse 1.5s infinite' : 'none',
                  }} />
                  <div>
                    <div className="text-xs font-bold text-[var(--text-primary)]">{item.data.title}</div>
                    {item.data.detail && <div className="text-[10px] text-[var(--text-secondary)] mt-0.5">{item.data.detail}</div>}
                  </div>
                </div>
              </div>
            );
          }

          return null;
        })}

        {!loadingHistory && messages.length === 0 && rawEvents.length === 0 && toolCalls.length === 0 && !streaming && (
          <div className="text-xs text-[var(--text-secondary)] text-center" style={{ padding: '32px 0' }}>发送消息开始</div>
        )}
      </div>

      <div className="shrink-0 border-t border-[var(--glass-border-strong)]" style={{ padding: '16px 20px' }}>
        <div className="flex" style={{ gap: 8 }}>
          <input
            value={input} onChange={e => setInput(e.target.value)}
            onKeyDown={e => e.key === 'Enter' && !e.shiftKey && handleSubmit()}
            placeholder="输入你的需求..." disabled={streaming}
            style={{ flex: 1, height: 40, padding: '0 16px', fontSize: 12 }}
            className="rounded-lg bg-[var(--bg)] border border-[var(--glass-border-strong)] text-[var(--text-primary)] placeholder:text-[var(--text-secondary)]/50 focus:outline-none focus:border-[var(--accent-light)] disabled:opacity-50"
          />
          {streaming ? (
            <button onClick={handleCancel} className="btn-danger" style={{ minWidth: 56, height: 40, fontSize: 12 }}>停止</button>
          ) : (
            <button onClick={handleSubmit} className="btn-primary" style={{ minWidth: 56, height: 40, fontSize: 12 }}>发送</button>
          )}
        </div>
      </div>
    </div>
  );
}
