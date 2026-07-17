import { useCallback, useEffect, useLayoutEffect, useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import type { ThemeMode, WorkspaceRecord } from '../types';
import { getFileTree, getFileContent, revealWorkspaceFile } from '../api/api';
import { ApiClientError } from '../api/client';

interface Props {
  workspace: WorkspaceRecord | null;
  onExitWorkspace: () => void;
  theme: ThemeMode;
}

// ====== 数据模型 ======

interface FlatEntry {
  name: string;
  path: string;
  type: 'file' | 'directory';
  size: number;
}

interface TreeNode {
  name: string;
  path: string;
  type: 'file' | 'directory';
  size: number;
  children: TreeNode[];
}

function buildTree(items: FlatEntry[]): TreeNode[] {
  const root: TreeNode[] = [];
  const byPath = new Map<string, TreeNode>();
  for (const item of items) {
    const parts = item.path.split('/').filter(Boolean);
    let siblings = root;
    let currentPath = '';
    parts.forEach((part, index) => {
      currentPath = currentPath ? `${currentPath}/${part}` : part;
      const existing = byPath.get(currentPath);
      if (existing) { siblings = existing.children; return; }
      const leaf = index === parts.length - 1;
      const node: TreeNode = {
        name: leaf ? item.name : part, path: currentPath,
        type: leaf ? item.type : 'directory', size: leaf ? item.size : 0, children: [],
      };
      byPath.set(currentPath, node);
      siblings.push(node);
      siblings = node.children;
    });
  }
  return sortNodes(root);
}

function sortNodes(nodes: TreeNode[]): TreeNode[] {
  return nodes
    .map((node) => ({ ...node, children: sortNodes(node.children) }))
    .sort((a, b) => {
      if (a.type !== b.type) return a.type === 'directory' ? -1 : 1;
      return a.name.localeCompare(b.name, undefined, { sensitivity: 'base' });
    });
}

// ====== 预览状态 ======
interface PreviewState {
  filePath: string;
  content: string;
  loading: boolean;
  error: string;
  targetRect: { top: number; right: number; bottom: number };
}

// ====== 主组件 ======
export function FileTree({ workspace, theme }: Props) {
  const [rootNodes, setRootNodes] = useState<TreeNode[]>([]);
  const [loading, setLoading] = useState(true);
  const [expanded, setExpanded] = useState<Set<string>>(new Set());
  const [preview, setPreview] = useState<PreviewState | null>(null);
  const [animOpen, setAnimOpen] = useState(false);
  const [fileActionMessage, setFileActionMessage] = useState('');
  const loadedWorkspace = useRef('');
  const listRef = useRef<HTMLDivElement>(null);
  const activeRowRef = useRef<HTMLButtonElement | null>(null);
  const animFrame = useRef(0);
  const exitTimer = useRef(0);
  const panelRectRef = useRef<DOMRect | null>(null);
  const previewRequest = useRef(0);
  const previewAbort = useRef<AbortController | null>(null);

  const loadRoot = useCallback(async () => {
    if (!workspace?.id) return;
    if (workspace.id === loadedWorkspace.current) return;
    loadedWorkspace.current = workspace.id;
    setLoading(true);
    try {
      const res = await getFileTree(workspace.id);
      const data = (res as unknown as { items?: FlatEntry[]; tree?: unknown }).items;
      if (Array.isArray(data)) setRootNodes(buildTree(data as FlatEntry[]));
      else setRootNodes([]);
    } catch { setRootNodes([]); }
    setLoading(false);
  }, [workspace?.id]);

  useEffect(() => { loadRoot(); }, [loadRoot]);

  useLayoutEffect(() => {
    const el = listRef.current?.closest('.glass-panel');
    if (el) panelRectRef.current = el.getBoundingClientRect();
  });

  const toggleDir = (path: string) => {
    setExpanded(prev => {
      const next = new Set(prev);
      if (next.has(path)) next.delete(path); else next.add(path);
      return next;
    });
  };

  const closePreview = useCallback(() => {
    previewAbort.current?.abort();
    ++previewRequest.current;
    clearTimeout(exitTimer.current);
    setAnimOpen(false);
    exitTimer.current = window.setTimeout(() => {
      setPreview(null);
      activeRowRef.current = null;
    }, 150);
  }, []);

  const openPreview = useCallback(async (filePath: string, rowEl: HTMLButtonElement) => {
    clearTimeout(exitTimer.current);
    cancelAnimationFrame(animFrame.current);

    // 点击同一文件 → 关闭
    if (preview?.filePath === filePath && animOpen) {
      closePreview();
      return;
    }

    previewAbort.current?.abort();
    const controller = new AbortController();
    previewAbort.current = controller;
    const requestId = ++previewRequest.current;
    const rect = rowEl.getBoundingClientRect();
    setPreview({
      filePath, content: '', loading: true, error: '',
      targetRect: { top: rect.top, right: rect.right, bottom: rect.bottom },
    });
    activeRowRef.current = rowEl;
    setAnimOpen(true);

    try {
      const res = await getFileContent(workspace?.id || '', filePath, controller.signal);
      if (requestId !== previewRequest.current || controller.signal.aborted) return;
      setPreview(prev => prev?.filePath === filePath ? { ...prev, content: res.content || '', loading: false } : prev);
    } catch (error) {
      if (controller.signal.aborted || requestId !== previewRequest.current) return;
      let message = '文件读取失败';
      if (error instanceof ApiClientError) {
        const code = (error.data as { error?: { code?: string } })?.error?.code;
        if (code === 'BINARY_FILE' || code === 'UNSUPPORTED_FILE_TYPE') message = '暂不支持预览此文件类型';
        else if (code === 'FILE_TOO_LARGE') message = '文件过大，预览上限为 10 MB';
        else if (code === 'FILE_NOT_FOUND') message = '文件已移动或删除';
        else message = `文件读取失败：${error.message}`;
      } else if (error instanceof Error) {
        message = `文件读取失败：${error.message}`;
      }
      setPreview(prev => prev?.filePath === filePath ? { ...prev, error: message, loading: false } : prev);
    }
  }, [preview?.filePath, animOpen, closePreview, workspace?.id]);

  const revealFile = useCallback(async (filePath: string) => {
    setFileActionMessage('');
    try {
      await revealWorkspaceFile(workspace?.id || '', filePath);
      setFileActionMessage('已在资源管理器中定位文件');
    } catch (error) {
      setFileActionMessage(error instanceof Error ? error.message : '无法在资源管理器中定位文件');
    }
  }, [workspace?.id]);

  useEffect(() => () => previewAbort.current?.abort(), []);

  // 滚动追踪：行离开 panel → 关闭
  useEffect(() => {
    const el = listRef.current;
    if (!el || !preview) return;
    const track = () => {
      cancelAnimationFrame(animFrame.current);
      animFrame.current = requestAnimationFrame(() => {
        const row = activeRowRef.current;
        const panel = panelRectRef.current;
        if (!row || !panel) return;
        const rect = row.getBoundingClientRect();
        const visible = rect.bottom > panel.top && rect.top < panel.bottom;
        if (!visible) {
          closePreview();
        } else if (preview) {
          setPreview(prev => prev ? { ...prev, targetRect: { top: rect.top, right: rect.right, bottom: rect.bottom } } : prev);
        }
      });
    };
    el.addEventListener('scroll', track, { passive: true });
    return () => {
      el.removeEventListener('scroll', track);
      cancelAnimationFrame(animFrame.current);
    };
  }, [preview, closePreview]);

  const popStyle: React.CSSProperties | undefined = preview ? (() => {
    const r = preview.targetRect;
    const maxH = window.innerHeight - 16;
    const top = Math.max(8, Math.min(r.top, maxH - Math.min(400, window.innerHeight - 120)));
    return {
      position: 'fixed',
      zIndex: 100,
      left: r.right + 10,
      top,
      width: 'min(520px, calc(100vw - 48px))',
      maxHeight: 'min(400px, calc(100vh - 120px))',
      opacity: animOpen ? 1 : 0,
      transform: animOpen ? 'translateX(0)' : 'translateX(-6px)',
      transition: 'opacity 0.12s ease-out, transform 0.15s ease-out',
    } as React.CSSProperties;
  })() : undefined;

  return (
    <div className="flex flex-col h-full">
      <div className="shrink-0 border-b border-[var(--glass-border-strong)]" style={{ padding: '12px 20px' }}>
        <div className="text-xs font-medium text-[var(--text-primary)] truncate">📁 {workspace?.name || '文件浏览'}</div>
        <div className="text-[10px] text-[var(--text-secondary)] truncate" style={{ marginTop: 4 }}>{workspace?.path || ''}</div>
        {fileActionMessage && <div className="text-[10px] text-[var(--accent-lighter)] truncate" style={{ marginTop: 3 }}>{fileActionMessage}</div>}
      </div>
      <div className="flex-1 overflow-y-auto" ref={listRef} style={{ padding: '8px 0' }}>
        {loading ? (
          <div className="text-xs text-[var(--text-secondary)] px-5 py-2">加载中...</div>
        ) : rootNodes.length === 0 ? (
          <div className="text-xs text-[var(--text-secondary)] px-5 py-2">暂无文件</div>
        ) : (
          rootNodes.map(node => (
            <FileTreeNode
              key={node.path} node={node} depth={0} expanded={expanded} onToggle={toggleDir}
              onPreview={openPreview} activePath={preview?.filePath ?? null}
              onReveal={revealFile}
            />
          ))
        )}
      </div>

      {preview && popStyle && createPortal(
        <div className={`theme-${theme} glass-panel flex flex-col overflow-hidden`} style={popStyle}>
          <div className="flex items-center justify-between shrink-0 border-b border-[var(--glass-border-strong)]"
            style={{ height: 36, padding: '0 10px 0 14px' }}>
            <span className="text-[10px] font-semibold text-[var(--text-primary)] truncate">
              📄 {preview.filePath.split('/').pop() || preview.filePath}
            </span>
            <button onClick={closePreview}
              style={{ width: 24, height: 24, borderRadius: '50%', flexShrink: 0,
                border: '1px solid var(--glass-border-strong)', background: 'var(--surface)',
                color: 'var(--text-secondary)', cursor: 'pointer', fontSize: 12,
                display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
              ✕
            </button>
          </div>
          <div className="flex-1 overflow-auto" style={{
            padding: '12px 14px', fontFamily: '"JetBrains Mono","Fira Code","Cascadia Code",monospace',
            fontSize: 10, lineHeight: 1.65,
          }}>
            {preview.loading ? (
              <span className="text-[10px] text-[var(--text-secondary)]">加载中...</span>
            ) : preview.error ? (
              <span className="text-[10px]" style={{ color: '#ef4444' }}>{preview.error}</span>
            ) : (
              <span className="text-[var(--text-primary)] whitespace-pre-wrap break-all">{preview.content || '(空文件)'}</span>
            )}
          </div>
        </div>,
        document.body
      )}
    </div>
  );
}

// ====== 树节点 ======
function FileTreeNode({
  node, depth, expanded, onToggle, onPreview, onReveal, activePath,
}: {
  node: TreeNode; depth: number; expanded: Set<string>;
  onToggle: (path: string) => void;
  onPreview: (path: string, el: HTMLButtonElement) => void;
  onReveal: (path: string) => void;
  activePath: string | null;
}) {
  const isDir = node.type === 'directory';
  const isOpen = expanded.has(node.path);
  const rowRef = useRef<HTMLButtonElement>(null);
  const clickTimer = useRef(0);

  const handleClick = () => {
    if (isDir) { onToggle(node.path); return; }
    clearTimeout(clickTimer.current);
    clickTimer.current = window.setTimeout(() => {
      if (rowRef.current) onPreview(node.path, rowRef.current);
    }, 180);
  };

  const handleDoubleClick = () => {
    if (isDir) return;
    clearTimeout(clickTimer.current);
    onReveal(node.path);
  };

  useEffect(() => () => clearTimeout(clickTimer.current), []);

  return (
    <div>
      <button ref={rowRef} type="button" onClick={handleClick} onDoubleClick={handleDoubleClick}
        className="w-full text-left flex items-center rounded hover:bg-[var(--accent)]/10 transition-colors"
        style={{ height: 32, paddingLeft: 8 + depth * 14, paddingRight: 8, gap: 6 }}>
        <span className="text-xs shrink-0" style={{ width: 14, textAlign: 'center', opacity: isDir ? 1 : 0 }}>
          {isOpen ? '▾' : '▸'}
        </span>
        <span className="text-xs shrink-0">
          {isDir ? (isOpen ? '📂' : '📁') : (activePath === node.path ? '📝' : '📄')}
        </span>
        <span className="text-xs text-[var(--text-primary)] truncate flex-1">{node.name}</span>
      </button>
      {isDir && isOpen && node.children.map(child => (
        <FileTreeNode key={child.path} node={child} depth={depth + 1}
          expanded={expanded} onToggle={onToggle} onPreview={onPreview} onReveal={onReveal} activePath={activePath} />
      ))}
    </div>
  );
}
