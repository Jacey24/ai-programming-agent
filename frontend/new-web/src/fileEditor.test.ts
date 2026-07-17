import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
import {
  DEFAULT_SESSION_WIDTH,
  MIN_SESSION_WIDTH,
  clampSessionWidth,
  editorLanguageForPath,
} from './fileEditor.ts';

describe('workspace file editor', () => {
  it('maps all supported text file types to Monaco languages', () => {
    const cases: Record<string, string> = {
      'a.cpp': 'cpp', 'a.c': 'c', 'a.h': 'cpp', 'a.hpp': 'cpp',
      'a.txt': 'plaintext', 'a.md': 'markdown', 'a.json': 'json',
      'a.yaml': 'yaml', 'a.yml': 'yaml', 'a.xml': 'xml', 'a.ini': 'ini',
      'a.toml': 'toml', 'a.cmake': 'cmake', 'CMakeLists.txt': 'cmake',
      'a.js': 'javascript', 'a.ts': 'typescript', 'a.tsx': 'typescript',
      'a.css': 'css', 'a.html': 'html',
    };
    for (const [path, expected] of Object.entries(cases)) {
      assert.equal(editorLanguageForPath(path), expected, path);
    }
  });

  it('keeps the session pane inside layout minimums', () => {
    assert.equal(clampSessionWidth(100, 1400, 28), MIN_SESSION_WIDTH);
    assert.equal(clampSessionWidth(DEFAULT_SESSION_WIDTH, 1400, 28), DEFAULT_SESSION_WIDTH);
    assert.equal(clampSessionWidth(900, 1100, 240), 496);
  });
});
