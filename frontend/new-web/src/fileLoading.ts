export interface FileRequestToken {
  requestId: number;
  workspaceId: string;
  filePath: string;
}

export function fileContentRequestUrl(baseUrl: string, filePath: string) {
  return `${baseUrl}?path=${encodeURIComponent(filePath)}`;
}

export function canApplyFileResponse(
  token: FileRequestToken,
  latestRequestId: number,
  currentWorkspaceId: string,
  currentFilePath: string,
  aborted: boolean,
) {
  return !aborted &&
    token.requestId === latestRequestId &&
    token.workspaceId === currentWorkspaceId &&
    token.filePath === currentFilePath;
}

export function filePreviewErrorMessage(error: unknown) {
  const code = error && typeof error === 'object' ?
    ((error as { data?: { error?: { code?: string } } }).data?.error?.code) :
    undefined;
  if (code === 'BINARY_FILE' || code === 'UNSUPPORTED_FILE_TYPE') {
    return '暂不支持预览此文件类型';
  }
  if (code === 'FILE_TOO_LARGE') return '文件过大，预览上限为 10 MB';
  if (code === 'FILE_NOT_FOUND') return '文件不存在或已移动';
  if (code === 'INVALID_PATH') return '无权访问此文件';
  if (code === 'WORKSPACE_NOT_FOUND') return '当前 Workspace 不存在';
  return '文件读取失败';
}
