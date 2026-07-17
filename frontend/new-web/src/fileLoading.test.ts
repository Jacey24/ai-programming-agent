import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
import {
  canApplyFileResponse,
  fileContentRequestUrl,
  filePreviewErrorMessage,
  type FileRequestToken,
} from './fileLoading.ts';

describe('workspace file loading', () => {
  it('encodes Windows separators, Chinese, spaces, #, and + in the request URL', () => {
    assert.equal(
      fileContentRequestUrl(
        '/api/v1/workspaces/ws-test/files/content',
        '中文 目录\\a # +.cpp',
      ),
      '/api/v1/workspaces/ws-test/files/content?path=' +
        '%E4%B8%AD%E6%96%87%20%E7%9B%AE%E5%BD%95%5Ca%20%23%20%2B.cpp',
    );
  });

  it('accepts only the latest request for the active Workspace and file', () => {
    const token: FileRequestToken = {
      requestId: 2,
      workspaceId: 'workspace-a',
      filePath: 'hello.cpp',
    };

    assert.equal(canApplyFileResponse(token, 2, 'workspace-a', 'hello.cpp', false), true);
    assert.equal(canApplyFileResponse(token, 3, 'workspace-a', 'hello.cpp', false), false);
    assert.equal(canApplyFileResponse(token, 2, 'workspace-b', 'hello.cpp', false), false);
    assert.equal(canApplyFileResponse(token, 2, 'workspace-a', 'dfs.cpp', false), false);
    assert.equal(canApplyFileResponse(token, 2, 'workspace-a', 'hello.cpp', true), false);
  });

  it('maps file errors without exposing backend paths', () => {
    const apiError = (code: string, message = 'C:\\private\\secret.txt') => ({
      message,
      data: { error: { code, message } },
    });

    assert.equal(filePreviewErrorMessage(apiError('FILE_NOT_FOUND')), '文件不存在或已移动');
    assert.equal(filePreviewErrorMessage(apiError('BINARY_FILE')), '暂不支持预览此文件类型');
    assert.equal(filePreviewErrorMessage(apiError('INVALID_PATH')), '无权访问此文件');
    assert.equal(filePreviewErrorMessage(apiError('UNKNOWN')), '文件读取失败');
  });
});
