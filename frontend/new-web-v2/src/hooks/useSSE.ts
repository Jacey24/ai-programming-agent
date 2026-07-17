import {useCallback, useRef, useState} from 'react';

import type {TaskEventRecord} from '../types';

interface UseSSEReturn {
  streamingContent: string;
  streaming: boolean;
  startSSE: (taskId: string) => void;
  closeSSE: () => void;
  rawEvents: TaskEventRecord[];
  appendEvents: (evts: TaskEventRecord[]) => void;
  setRawEvents: React.Dispatch<React.SetStateAction<TaskEventRecord[]>>;
}

export function useSSE(
    addMessage: (msg: {
      id: string; role: 'user' | 'assistant'; content: string;
      expert?: string; timestamp: string
    }) => void,
    ): UseSSEReturn {
  const [streaming, setStreaming] = useState(false);
  const [streamingContent, setStreamingContent] = useState('');
  const [rawEvents, setRawEvents] = useState<TaskEventRecord[]>([]);
  const eventSource = useRef<EventSource|null>(null);
  const seenIds = useRef<Set<string>>(new Set());
  const streamingRef = useRef('');

  const closeSSE = useCallback(() => {
    eventSource.current?.close();
    eventSource.current = null;
    setStreaming(false);
    streamingRef.current = '';
    setStreamingContent('');
  }, []);

  const appendEvents = useCallback((evts: TaskEventRecord[]) => {
    setRawEvents(prev => {
      const existing = new Set(prev.map(e => e.id));
      const next = [...prev];
      for (const e of evts) {
        if (e.id && existing.has(e.id)) continue;
        if (e.id) existing.add(e.id);
        next.push(e);
      }
      return next;
    });
  }, []);

  const startSSE = useCallback((taskId: string) => {
    closeSSE();
    seenIds.current.clear();
    setRawEvents([]);
    streamingRef.current = '';
    setStreamingContent('');
    setStreaming(true);

    const source = new EventSource(`/api/v1/tasks/${taskId}/events`);
    eventSource.current = source;

    const handler = (e: MessageEvent) => {
      try {
        const evt: TaskEventRecord = JSON.parse(e.data);
        const evtId = evt.id || `${evt.type}:${evt.created_at}`;
        if (seenIds.current.has(evtId)) return;
        seenIds.current.add(evtId);

        // Streaming chunk
        if (evt.type === 'agent_message_chunk' && evt.content) {
          streamingRef.current += evt.content;
          setStreamingContent(streamingRef.current);
          return;
        }

        // Agent message
        if (evt.type === 'agent_message' &&
            ((evt.metadata as {channel?: string})?.channel) === 'dialog' &&
            evt.content) {
          const meta = evt.metadata as {
            stream_end?: boolean;
            expert?: string
          };
          const isStreamEnd = meta.stream_end === true;
          if (isStreamEnd && streamingRef.current) {
            addMessage({
              id: evtId,
              role: 'assistant',
              content: streamingRef.current,
              expert: meta.expert,
              timestamp: evt.created_at || '',
            });
            streamingRef.current = '';
            setStreamingContent('');
          } else if (!isStreamEnd) {
            addMessage({
              id: evtId,
              role: 'assistant',
              content: evt.content,
              expert: meta.expert,
              timestamp: evt.created_at || '',
            });
          }
        }

        appendEvents([evt]);
      } catch { /* ignore parse errors */
      }
    };

    // Register all event type listeners
    const eventTypes = [
      'agent_message',
      'agent_message_chunk',
      'tool_started',
      'tool_output',
      'tool_finished',
      'task_planning',
      'permission_required',
      'permission_resolved',
      'file_changed',
      'task_completed',
      'task_failed',
      'task_cancelled',
      'stream_end',
    ];
    for (const type of eventTypes) {
      source.addEventListener(type, handler);
    }

    source.onerror = () => {
      setStreaming(false);
      closeSSE();
    };
  }, [addMessage, appendEvents, closeSSE]);

  return {
    streamingContent,
    streaming,
    startSSE,
    closeSSE,
    rawEvents,
    appendEvents,
    setRawEvents,
  };
}