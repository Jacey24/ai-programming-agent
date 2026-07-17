export const MIN_GRAPH_WIDTH = 320;
export const MIN_SESSION_WIDTH = 320;
export const DEFAULT_SESSION_WIDTH = 430;
export const LAYOUT_STORAGE_KEY = 'codepilot.workspace.session-pane-width';

export function editorLanguageForPath(path: string, backendLanguage = '') {
  const name = path.split(/[\\/]/).pop()?.toLowerCase() || '';
  if (name === 'cmakelists.txt') return 'cmake';
  const dot = name.lastIndexOf('.');
  const ext = dot >= 0 ? name.slice(dot) : '';
  const languages: Record<string, string> = {
    '.c': 'c', '.cpp': 'cpp', '.cc': 'cpp', '.cxx': 'cpp',
    '.h': 'cpp', '.hpp': 'cpp', '.txt': 'plaintext', '.md': 'markdown',
    '.json': 'json', '.yaml': 'yaml', '.yml': 'yaml', '.xml': 'xml',
    '.ini': 'ini', '.toml': 'toml', '.cmake': 'cmake',
    '.js': 'javascript', '.jsx': 'javascript', '.ts': 'typescript',
    '.tsx': 'typescript', '.css': 'css', '.html': 'html', '.htm': 'html',
  };
  return languages[ext] || backendLanguage || 'plaintext';
}

export function clampSessionWidth(
  desired: number,
  containerWidth: number,
  toolWidth: number,
) {
  const fixedSpace = toolWidth + MIN_GRAPH_WIDTH + 44;
  const maximum = Math.max(MIN_SESSION_WIDTH, containerWidth - fixedSpace);
  return Math.min(Math.max(desired, MIN_SESSION_WIDTH), maximum);
}

export function fileSaveErrorMessage(error: unknown) {
  if (error && typeof error === 'object') {
    const apiError = error as { message?: string; data?: { error?: { code?: string } } };
    const code = apiError.data?.error?.code;
    if (code === 'INVALID_PATH') return '无权写入此文件';
    if (code === 'FILE_NOT_FOUND') return '文件不存在或已移动';
    if (code === 'FILE_TOO_LARGE') return '文件过大，无法保存';
    if (code === 'BINARY_FILE') return '暂不支持保存此文件类型';
    if (code === 'WORKSPACE_NOT_FOUND') return '当前 Workspace 不存在';
    if (code === 'ENCODING_ERROR') return '文件编码转换失败，未保存任何内容';
    if (apiError.message) return apiError.message;
  }
  return error instanceof Error && error.message ? error.message : '文件保存失败';
}
