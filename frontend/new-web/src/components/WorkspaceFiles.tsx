import Editor from '@monaco-editor/react';
import {
  forwardRef,
  useCallback,
  useEffect,
  useImperativeHandle,
  useRef,
  useState,
} from 'react';
import type { ThemeMode, WorkspaceFileContent, WorkspaceRecord } from '../types';
import {
  getFileContent,
  getFileTree,
  saveWorkspaceFile,
} from '../api/api';
import {
  canApplyFileResponse,
  filePreviewErrorMessage,
  type FileRequestToken,
} from '../fileLoading';
import { editorLanguageForPath, fileSaveErrorMessage } from '../fileEditor';

interface Props {
  workspace: WorkspaceRecord | null;
  onExitWorkspace: () => void;
  onDirtyChange?: (dirty: boolean) => void;
  theme: ThemeMode;
}

export interface WorkspaceFilesHandle {
  requestExitWorkspace: () => void;
}

interface FlatEntry {
  name: string;
  path: string;
  type: 'file'|'directory';
  size: number;
}

interface TreeNode extends FlatEntry {
  children: TreeNode[];
}

interface EditorState {
  workspaceId: string;
  filePath: string;
  fileName: string;
  originalContent: string;
  content: string;
  dirty: boolean;
  saving: boolean;
  loading: boolean;
  loadingError: string;
  saveError: string;
  saveMessage: string;
  language: string;
  encoding: WorkspaceFileContent['encoding'];
  requestId: number;
}

type PendingAction =
  | { kind: 'list' }
  | { kind: 'open'; filePath: string }
  | { kind: 'exit' };

function buildTree(items: FlatEntry[]) {
  const root: TreeNode[] = [];
  const byPath = new Map<string, TreeNode>();
  for (const item of items) {
    const parts = item.path.split('/').filter(Boolean);
    let siblings = root;
    let currentPath = '';
    parts.forEach((part, index) => {
      currentPath = currentPath ? `${currentPath}/${part}` : part;
      const existing = byPath.get(currentPath);
      if (existing) {
        siblings = existing.children;
        return;
      }
      const leaf = index === parts.length - 1;
      const node: TreeNode = {
        name: leaf ? item.name : part,
        path: currentPath,
        type: leaf ? item.type : 'directory',
        size: leaf ? item.size : 0,
        children: [],
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
    .map(node => ({ ...node, children: sortNodes(node.children) }))
    .sort((a, b) => {
      if (a.type !== b.type) return a.type === 'directory' ? -1 : 1;
      return a.name.localeCompare(b.name, undefined, { sensitivity: 'base' });
    });
}

export const WorkspaceFiles = forwardRef<WorkspaceFilesHandle, Props>(function WorkspaceFiles(
  { workspace, onExitWorkspace, onDirtyChange, theme },
  ref,
) {
  const [rootNodes, setRootNodes] = useState<TreeNode[]>([]);
  const [treeLoading, setTreeLoading] = useState(true);
  const [expanded, setExpanded] = useState<Set<string>>(new Set());
  const [editor, setEditor] = useState<EditorState | null>(null);
  const [pendingAction, setPendingAction] = useState<PendingAction | null>(null);
  const loadedWorkspace = useRef('');
  const editorRef = useRef<EditorState | null>(null);
  const workspaceId = workspace?.id || '';
  const workspaceIdRef = useRef(workspaceId);
  const previewRequest = useRef(0);
  const previewAbort = useRef<AbortController | null>(null);
  const treeRequest = useRef(0);
  const treeAbort = useRef<AbortController | null>(null);
  const saveRequest = useRef(0);
  const saveAbort = useRef<AbortController | null>(null);
  const savingRef = useRef(false);
  editorRef.current = editor;
  workspaceIdRef.current = workspaceId;

  useEffect(() => {
    onDirtyChange?.(Boolean(editor?.dirty));
  }, [editor?.dirty, onDirtyChange]);

  useEffect(() => {
    const handleBeforeUnload = (event: BeforeUnloadEvent) => {
      if (!editorRef.current?.dirty) return;
      event.preventDefault();
      event.returnValue = '';
    };
    window.addEventListener('beforeunload', handleBeforeUnload);
    return () => window.removeEventListener('beforeunload', handleBeforeUnload);
  }, []);

  useEffect(() => {
    treeAbort.current?.abort();
    previewAbort.current?.abort();
    saveAbort.current?.abort();
    ++treeRequest.current;
    ++previewRequest.current;
    ++saveRequest.current;
    savingRef.current = false;
    loadedWorkspace.current = '';
    setRootNodes([]);
    setExpanded(new Set());
    setEditor(null);
    setPendingAction(null);
  }, [workspaceId]);

  const loadRoot = useCallback(async () => {
    if (!workspaceId || workspaceId === loadedWorkspace.current) return;
    treeAbort.current?.abort();
    const controller = new AbortController();
    treeAbort.current = controller;
    const requestId = ++treeRequest.current;
    const requestWorkspaceId = workspaceId;
    loadedWorkspace.current = requestWorkspaceId;
    setTreeLoading(true);
    try {
      const response = await getFileTree(requestWorkspaceId, controller.signal);
      if (controller.signal.aborted || requestId !== treeRequest.current ||
          workspaceIdRef.current !== requestWorkspaceId) return;
      setRootNodes(Array.isArray(response.items) ? buildTree(response.items) : []);
    } catch {
      if (!controller.signal.aborted && requestId === treeRequest.current &&
          workspaceIdRef.current === requestWorkspaceId) setRootNodes([]);
    } finally {
      if (!controller.signal.aborted && requestId === treeRequest.current &&
          workspaceIdRef.current === requestWorkspaceId) setTreeLoading(false);
    }
  }, [workspaceId]);

  useEffect(() => { void loadRoot(); }, [loadRoot]);

  const loadFile = useCallback(async (filePath: string) => {
    if (!workspaceId) return;
    previewAbort.current?.abort();
    const controller = new AbortController();
    previewAbort.current = controller;
    const requestId = ++previewRequest.current;
    const token: FileRequestToken = { requestId, workspaceId, filePath };
    const fileName = filePath.split('/').pop() || filePath;
    setEditor({
      workspaceId,
      filePath,
      fileName,
      originalContent: '',
      content: '',
      dirty: false,
      saving: false,
      loading: true,
      loadingError: '',
      saveError: '',
      saveMessage: '',
      language: editorLanguageForPath(filePath),
      encoding: 'utf-8',
      requestId,
    });
    try {
      const response = await getFileContent(workspaceId, filePath, controller.signal);
      if (!canApplyFileResponse(
        token, previewRequest.current, workspaceIdRef.current, filePath,
        controller.signal.aborted,
      )) return;
      setEditor(current => current && canApplyFileResponse(
        token,
        previewRequest.current,
        current.workspaceId,
        current.filePath,
        controller.signal.aborted,
      ) ? {
        ...current,
        fileName: response.name || fileName,
        originalContent: response.content,
        content: response.content,
        language: editorLanguageForPath(filePath, response.language),
        encoding: response.encoding || 'utf-8',
        loading: false,
      } : current);
    } catch (error) {
      if (!canApplyFileResponse(
        token, previewRequest.current, workspaceIdRef.current, filePath,
        controller.signal.aborted,
      )) return;
      setEditor(current => current && current.requestId === requestId ? {
        ...current,
        loading: false,
        loadingError: filePreviewErrorMessage(error),
      } : current);
    }
  }, [workspaceId]);

  const executeAction = useCallback((action: PendingAction) => {
    if (action.kind === 'open') {
      void loadFile(action.filePath);
    } else if (action.kind === 'list') {
      previewAbort.current?.abort();
      ++previewRequest.current;
      setEditor(null);
    } else {
      onExitWorkspace();
    }
  }, [loadFile, onExitWorkspace]);

  const requestAction = useCallback((action: PendingAction) => {
    if (editorRef.current?.dirty) {
      setPendingAction(action);
      return;
    }
    executeAction(action);
  }, [executeAction]);

  useImperativeHandle(ref, () => ({
    requestExitWorkspace: () => requestAction({ kind: 'exit' }),
  }), [requestAction]);

  const saveCurrent = useCallback(async () => {
    const current = editorRef.current;
    if (!current || current.loading || current.loadingError || savingRef.current) return false;
    if (!current.dirty) return true;
    if (current.workspaceId !== workspaceIdRef.current) {
      setEditor(value => value ? { ...value, saveError: 'Workspace 已切换，已阻止保存旧文件' } : value);
      return false;
    }
    savingRef.current = true;
    saveAbort.current?.abort();
    const controller = new AbortController();
    saveAbort.current = controller;
    const requestId = ++saveRequest.current;
    const snapshot = {
      workspaceId: current.workspaceId,
      filePath: current.filePath,
      content: current.content,
      encoding: current.encoding,
    };
    setEditor(value => value ? {
      ...value,
      saving: true,
      saveError: '',
      saveMessage: '',
    } : value);
    try {
      const response = await saveWorkspaceFile(
        snapshot.workspaceId,
        snapshot.filePath,
        snapshot.content,
        snapshot.encoding,
        controller.signal,
      );
      if (controller.signal.aborted || requestId !== saveRequest.current ||
          workspaceIdRef.current !== snapshot.workspaceId) return false;
      setEditor(value => value &&
        value.workspaceId === snapshot.workspaceId &&
        value.filePath === snapshot.filePath ? {
          ...value,
          originalContent: snapshot.content,
          dirty: value.content !== snapshot.content,
          saving: false,
          saveError: '',
          saveMessage: '保存成功',
          encoding: response.encoding || value.encoding,
        } : value);
      return true;
    } catch (error) {
      if (!controller.signal.aborted && requestId === saveRequest.current &&
          workspaceIdRef.current === snapshot.workspaceId) {
        setEditor(value => value && value.filePath === snapshot.filePath ? {
          ...value,
          saving: false,
          saveError: fileSaveErrorMessage(error),
          saveMessage: '',
        } : value);
      }
      return false;
    } finally {
      if (requestId === saveRequest.current) savingRef.current = false;
    }
  }, []);

  useEffect(() => {
    const handleSaveShortcut = (event: KeyboardEvent) => {
      if (!(event.ctrlKey || event.metaKey) || event.key.toLowerCase() !== 's') return;
      if (!editorRef.current) return;
      event.preventDefault();
      void saveCurrent();
    };
    window.addEventListener('keydown', handleSaveShortcut);
    return () => window.removeEventListener('keydown', handleSaveShortcut);
  }, [saveCurrent]);

  const resolvePending = useCallback(async (choice: 'save'|'discard'|'cancel') => {
    const action = pendingAction;
    if (!action) return;
    if (choice === 'cancel') {
      setPendingAction(null);
      return;
    }
    if (choice === 'save' && !(await saveCurrent())) return;
    setPendingAction(null);
    executeAction(action);
  }, [executeAction, pendingAction, saveCurrent]);

  useEffect(() => () => {
    treeAbort.current?.abort();
    previewAbort.current?.abort();
    saveAbort.current?.abort();
  }, []);

  const toggleDir = (path: string) => setExpanded(current => {
    const next = new Set(current);
    if (next.has(path)) next.delete(path); else next.add(path);
    return next;
  });

  if (editor) {
    return (
      <div className="relative flex h-full min-h-0 flex-col overflow-hidden">
        <div className="flex shrink-0 items-center border-b border-[var(--glass-border-strong)]" style={{ height: 54, padding: '0 20px', gap: 8 }}>
          <button
            type="button"
            onClick={() => requestAction({ kind: 'list' })}
            className="rounded-lg border border-[var(--glass-border-strong)] text-xs text-[var(--text-secondary)] hover:text-[var(--text-primary)]"
            style={{ height: 30, padding: '0 10px', fontSize: 12, display: 'flex', alignItems: 'center', justifyContent: 'center' }}
          >
            ← 文件列表
          </button>
          <div className="min-w-0 flex-1">
            <div className="truncate text-xs font-semibold text-[var(--text-primary)]">
              {editor.fileName}{editor.dirty ? ' •' : ''}
            </div>
            <div className="truncate font-mono text-[10px] text-[var(--text-secondary)]" title={editor.filePath}>
              {editor.filePath}
            </div>
          </div>
          <div className="shrink-0 text-[10px]" aria-live="polite">
            {editor.saving ? <span className="text-[var(--text-secondary)]">保存中...</span> : null}
            {!editor.saving && editor.dirty ? <span className="text-amber-400">未保存</span> : null}
            {!editor.saving && !editor.dirty && editor.saveMessage ? <span className="text-emerald-400">{editor.saveMessage}</span> : null}
          </div>
          <button
            type="button"
            disabled={!editor.dirty || editor.saving || editor.loading || Boolean(editor.loadingError)}
            onClick={() => void saveCurrent()}
            style={{ height: 32, padding: '0 16px', borderRadius: 7, fontSize: 12, fontWeight: 600, cursor: 'pointer', background: 'var(--accent)', color: '#fff', border: 'none', flexShrink: 0, display: 'flex', alignItems: 'center', justifyContent: 'center' }}
          >
            保存
          </button>
        </div>
        {editor.saveError ? (
          <div className="shrink-0 border-b border-red-500/30 bg-red-500/10 px-3 py-2 text-xs text-red-300" role="alert">
            保存失败：{editor.saveError}
          </div>
        ) : null}
        <div className="min-h-0 flex-1 overflow-hidden">
          {editor.loading ? (
            <div className="grid h-full place-items-center text-xs text-[var(--text-secondary)]">正在读取文件...</div>
          ) : editor.loadingError ? (
            <div className="grid h-full place-items-center p-6 text-center text-sm text-[var(--text-secondary)]">
              {editor.loadingError}
            </div>
          ) : (
            <Editor
              path={`${editor.workspaceId}/${editor.filePath}`}
              language={editor.language}
              value={editor.content}
              theme={theme === 'dark' ? 'vs-dark' : 'light'}
              onChange={value => setEditor(current => current ? {
                ...current,
                content: value ?? '',
                dirty: (value ?? '') !== current.originalContent,
                saveError: '',
                saveMessage: '',
              } : current)}
              options={{
                automaticLayout: true,
                readOnly: editor.saving,
                minimap: { enabled: true },
                fontSize: 13,
                lineNumbers: 'on',
                scrollBeyondLastLine: false,
                wordWrap: 'off',
                renderWhitespace: 'selection',
                folding: true,
                formatOnPaste: false,
                formatOnType: false,
              }}
            />
          )}
        </div>
        {pendingAction ? (
          <UnsavedPrompt
            saving={editor.saving}
            onChoice={choice => void resolvePending(choice)}
          />
        ) : null}
      </div>
    );
  }

  return (
    <div className="flex h-full min-h-0 flex-col overflow-hidden">
      <div className="flex shrink-0 items-center border-b border-[var(--glass-border-strong)]" style={{ height: 54, padding: '0 20px' }}>
        <div className="min-w-0 flex-1 truncate text-xs font-medium text-[var(--text-primary)]">
          📁 {workspace?.name || '文件浏览'}
        </div>
        <span className="text-[10px] text-[var(--text-secondary)]">单击文件进行编辑</span>
      </div>
      <div className="min-h-0 flex-1 overflow-y-auto py-3">
        {treeLoading ? (
          <div className="px-5 py-2 text-xs text-[var(--text-secondary)]">加载中...</div>
        ) : rootNodes.length === 0 ? (
          <div className="px-5 py-2 text-xs text-[var(--text-secondary)]">暂无文件</div>
        ) : rootNodes.map(node => (
          <FileTreeRow
            key={node.path}
            node={node}
            depth={0}
            expanded={expanded}
            onToggle={toggleDir}
            onOpen={filePath => requestAction({ kind: 'open', filePath })}
          />
        ))}
      </div>
    </div>
  );
});

function UnsavedPrompt({
  saving,
  onChoice,
}: {
  saving: boolean;
  onChoice: (choice: 'save'|'discard'|'cancel') => void;
}) {
  return (
    <div className="absolute inset-0 z-30 grid place-items-center bg-black/35 p-4 backdrop-blur-sm">
      <div className="glass-panel flex max-w-sm flex-col gap-4 p-5" role="dialog" aria-modal="true" aria-labelledby="unsaved-title">
        <div>
          <div id="unsaved-title" className="text-sm font-semibold text-[var(--text-primary)]">文件尚未保存</div>
          <div className="mt-1 text-xs leading-5 text-[var(--text-secondary)]">请选择保存修改、放弃修改，或取消当前操作。</div>
        </div>
        <div className="flex flex-wrap justify-end gap-2">
          <button type="button" disabled={saving} onClick={() => onChoice('cancel')}
            className="rounded-lg border border-[var(--glass-border-strong)] px-3 py-1.5 text-xs text-[var(--text-secondary)] disabled:opacity-50">取消返回</button>
          <button type="button" disabled={saving} onClick={() => onChoice('discard')}
            className="btn-danger px-3 py-1.5 text-xs disabled:opacity-50">放弃修改</button>
          <button type="button" disabled={saving} onClick={() => onChoice('save')}
            className="btn-primary px-3 py-1.5 text-xs disabled:opacity-50">保存并继续</button>
        </div>
      </div>
    </div>
  );
}

function FileTreeRow({
  node,
  depth,
  expanded,
  onToggle,
  onOpen,
}: {
  node: TreeNode;
  depth: number;
  expanded: Set<string>;
  onToggle: (path: string) => void;
  onOpen: (path: string) => void;
}) {
  const isDirectory = node.type === 'directory';
  const isOpen = expanded.has(node.path);
  return (
    <div style={{ marginBottom: 4 }}>
      <button
        type="button"
        onClick={() => {
          if (isDirectory) onToggle(node.path);
          else onOpen(node.path);
        }}
        className="flex w-full items-center gap-1.5 rounded-md pr-3 text-left text-xs text-[var(--text-secondary)] hover:bg-[var(--accent)]/10 hover:text-[var(--text-primary)]"
        style={{ height: 38, paddingLeft: 16 + depth * 16 }}
        title={node.path}
      >
        <span className="w-3 shrink-0 text-center">{isDirectory ? (isOpen ? '▾' : '▸') : ''}</span>
        <span className="shrink-0">{isDirectory ? '📁' : '📄'}</span>
        <span className="truncate">{node.name}</span>
      </button>
      {isDirectory && isOpen ? node.children.map(child => (
        <FileTreeRow
          key={child.path}
          node={child}
          depth={depth + 1}
          expanded={expanded}
          onToggle={onToggle}
          onOpen={onOpen}
        />
      )) : null}
    </div>
  );
}