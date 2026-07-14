# Details

Date : 2026-07-14 15:56:23

Directory d:\\VS Code stuff\\ai-programming-agent\\frontend

Total : 58 files,  8504 codes, 83 comments, 575 blanks, all 9162 lines

[Summary](results.md) / Details / [Diff Summary](diff.md) / [Diff Details](diff-details.md)

## Files
| filename | language | code | comment | blank | total |
| :--- | :--- | ---: | ---: | ---: | ---: |
| [frontend/api-client/AgentApiClient.cpp](/frontend/api-client/AgentApiClient.cpp) | C++ | 225 | 18 | 52 | 295 |
| [frontend/api-client/AgentApiClient.h](/frontend/api-client/AgentApiClient.h) | C++ | 47 | 14 | 17 | 78 |
| [frontend/api-client/EventStreamClient.cpp](/frontend/api-client/EventStreamClient.cpp) | C++ | 160 | 23 | 35 | 218 |
| [frontend/api-client/EventStreamClient.h](/frontend/api-client/EventStreamClient.h) | C++ | 35 | 11 | 13 | 59 |
| [frontend/app/TuiApplication.cpp](/frontend/app/TuiApplication.cpp) | C++ | 0 | 1 | 1 | 2 |
| [frontend/components/DiffViewer.cpp](/frontend/components/DiffViewer.cpp) | C++ | 0 | 1 | 1 | 2 |
| [frontend/components/InputBox.cpp](/frontend/components/InputBox.cpp) | C++ | 0 | 1 | 1 | 2 |
| [frontend/components/LogPanel.cpp](/frontend/components/LogPanel.cpp) | C++ | 0 | 1 | 1 | 2 |
| [frontend/components/MessageList.cpp](/frontend/components/MessageList.cpp) | C++ | 0 | 1 | 1 | 2 |
| [frontend/components/StatusPanel.cpp](/frontend/components/StatusPanel.cpp) | C++ | 0 | 1 | 1 | 2 |
| [frontend/state/EventStore.cpp](/frontend/state/EventStore.cpp) | C++ | 0 | 1 | 1 | 2 |
| [frontend/state/TaskStateModel.cpp](/frontend/state/TaskStateModel.cpp) | C++ | 0 | 1 | 1 | 2 |
| [frontend/state/UiState.cpp](/frontend/state/UiState.cpp) | C++ | 0 | 1 | 1 | 2 |
| [frontend/views/ChatView.cpp](/frontend/views/ChatView.cpp) | C++ | 0 | 1 | 1 | 2 |
| [frontend/views/HistoryView.cpp](/frontend/views/HistoryView.cpp) | C++ | 0 | 1 | 1 | 2 |
| [frontend/views/PermissionDialog.cpp](/frontend/views/PermissionDialog.cpp) | C++ | 0 | 1 | 1 | 2 |
| [frontend/views/TaskView.cpp](/frontend/views/TaskView.cpp) | C++ | 0 | 1 | 1 | 2 |
| [frontend/views/ToolOutputView.cpp](/frontend/views/ToolOutputView.cpp) | C++ | 0 | 1 | 1 | 2 |
| [frontend/views/WorkspaceView.cpp](/frontend/views/WorkspaceView.cpp) | C++ | 0 | 1 | 1 | 2 |
| [frontend/web/index.html](/frontend/web/index.html) | HTML | 12 | 0 | 1 | 13 |
| [frontend/web/package-lock.json](/frontend/web/package-lock.json) | JSON | 3,898 | 0 | 1 | 3,899 |
| [frontend/web/package.json](/frontend/web/package.json) | JSON | 29 | 0 | 1 | 30 |
| [frontend/web/src/App.tsx](/frontend/web/src/App.tsx) | TypeScript JSX | 208 | 0 | 16 | 224 |
| [frontend/web/src/api/client.ts](/frontend/web/src/api/client.ts) | TypeScript | 52 | 0 | 14 | 66 |
| [frontend/web/src/api/endpoints.ts](/frontend/web/src/api/endpoints.ts) | TypeScript | 46 | 0 | 2 | 48 |
| [frontend/web/src/api/healthApi.ts](/frontend/web/src/api/healthApi.ts) | TypeScript | 6 | 0 | 2 | 8 |
| [frontend/web/src/api/permissionApi.ts](/frontend/web/src/api/permissionApi.ts) | TypeScript | 19 | 0 | 4 | 23 |
| [frontend/web/src/api/taskApi.ts](/frontend/web/src/api/taskApi.ts) | TypeScript | 101 | 0 | 16 | 117 |
| [frontend/web/src/api/workspaceApi.ts](/frontend/web/src/api/workspaceApi.ts) | TypeScript | 14 | 0 | 5 | 19 |
| [frontend/web/src/components/agent/AgentPanel.tsx](/frontend/web/src/components/agent/AgentPanel.tsx) | TypeScript JSX | 403 | 0 | 33 | 436 |
| [frontend/web/src/components/agent/PermissionPrompt.tsx](/frontend/web/src/components/agent/PermissionPrompt.tsx) | TypeScript JSX | 113 | 0 | 17 | 130 |
| [frontend/web/src/components/chat/AgentChatPanel.tsx](/frontend/web/src/components/chat/AgentChatPanel.tsx) | TypeScript JSX | 55 | 0 | 6 | 61 |
| [frontend/web/src/components/chat/ChatComposer.tsx](/frontend/web/src/components/chat/ChatComposer.tsx) | TypeScript JSX | 106 | 0 | 9 | 115 |
| [frontend/web/src/components/chat/ChatMessageList.tsx](/frontend/web/src/components/chat/ChatMessageList.tsx) | TypeScript JSX | 307 | 0 | 36 | 343 |
| [frontend/web/src/components/chat/FileChangeCard.tsx](/frontend/web/src/components/chat/FileChangeCard.tsx) | TypeScript JSX | 32 | 0 | 4 | 36 |
| [frontend/web/src/components/chat/ToolCallCard.tsx](/frontend/web/src/components/chat/ToolCallCard.tsx) | TypeScript JSX | 38 | 0 | 5 | 43 |
| [frontend/web/src/components/editor/CodeEditor.tsx](/frontend/web/src/components/editor/CodeEditor.tsx) | TypeScript JSX | 71 | 0 | 5 | 76 |
| [frontend/web/src/components/editor/EditorShell.tsx](/frontend/web/src/components/editor/EditorShell.tsx) | TypeScript JSX | 285 | 0 | 25 | 310 |
| [frontend/web/src/components/editor/EditorTabs.tsx](/frontend/web/src/components/editor/EditorTabs.tsx) | TypeScript JSX | 52 | 0 | 5 | 57 |
| [frontend/web/src/components/editor/EmptyViewer.tsx](/frontend/web/src/components/editor/EmptyViewer.tsx) | TypeScript JSX | 21 | 0 | 3 | 24 |
| [frontend/web/src/components/explorer/FileExplorer.tsx](/frontend/web/src/components/explorer/FileExplorer.tsx) | TypeScript JSX | 62 | 0 | 5 | 67 |
| [frontend/web/src/components/explorer/FileTree.tsx](/frontend/web/src/components/explorer/FileTree.tsx) | TypeScript JSX | 131 | 0 | 15 | 146 |
| [frontend/web/src/components/history/HistoryPage.tsx](/frontend/web/src/components/history/HistoryPage.tsx) | TypeScript JSX | 173 | 0 | 15 | 188 |
| [frontend/web/src/components/layout/ActivityBar.tsx](/frontend/web/src/components/layout/ActivityBar.tsx) | TypeScript JSX | 40 | 0 | 5 | 45 |
| [frontend/web/src/components/layout/StatusBar.tsx](/frontend/web/src/components/layout/StatusBar.tsx) | TypeScript JSX | 36 | 0 | 3 | 39 |
| [frontend/web/src/components/layout/TerminalHeader.tsx](/frontend/web/src/components/layout/TerminalHeader.tsx) | TypeScript JSX | 83 | 0 | 9 | 92 |
| [frontend/web/src/components/layout/WorkspaceExplorer.tsx](/frontend/web/src/components/layout/WorkspaceExplorer.tsx) | TypeScript JSX | 330 | 0 | 25 | 355 |
| [frontend/web/src/hooks/useAgentRuntime.ts](/frontend/web/src/hooks/useAgentRuntime.ts) | TypeScript | 450 | 2 | 54 | 506 |
| [frontend/web/src/hooks/useClock.ts](/frontend/web/src/hooks/useClock.ts) | TypeScript | 22 | 0 | 6 | 28 |
| [frontend/web/src/hooks/useWorkspaceWorkbench.ts](/frontend/web/src/hooks/useWorkspaceWorkbench.ts) | TypeScript | 64 | 0 | 12 | 76 |
| [frontend/web/src/index.css](/frontend/web/src/index.css) | PostCSS | 73 | 0 | 17 | 90 |
| [frontend/web/src/main.tsx](/frontend/web/src/main.tsx) | TypeScript JSX | 13 | 0 | 5 | 18 |
| [frontend/web/src/store/agentReducer.ts](/frontend/web/src/store/agentReducer.ts) | TypeScript | 333 | 0 | 17 | 350 |
| [frontend/web/src/store/workbenchReducer.ts](/frontend/web/src/store/workbenchReducer.ts) | TypeScript | 120 | 0 | 7 | 127 |
| [frontend/web/src/types.ts](/frontend/web/src/types.ts) | TypeScript | 33 | 0 | 10 | 43 |
| [frontend/web/src/types/api.ts](/frontend/web/src/types/api.ts) | TypeScript | 171 | 0 | 25 | 196 |
| [frontend/web/tsconfig.json](/frontend/web/tsconfig.json) | JSON with Comments | 21 | 0 | 1 | 22 |
| [frontend/web/vite.config.ts](/frontend/web/vite.config.ts) | TypeScript | 14 | 0 | 2 | 16 |

[Summary](results.md) / Details / [Diff Summary](diff.md) / [Diff Details](diff-details.md)