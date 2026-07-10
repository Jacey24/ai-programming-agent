import type { OpenFileTab, WorkspaceFileContent, WorkspaceFileEntry } from "../types/api";

export interface WorkbenchState {
  fileTree: {
    status: "idle" | "loading" | "success" | "error";
    items: WorkspaceFileEntry[];
    error: string;
  };
  expandedDirectories: string[];
  openFiles: OpenFileTab[];
  activeFilePath: string;
  bottomPanel: "terminal" | "output" | "problems" | "task-log" | null;
}

export type WorkbenchAction =
  | { type: "treeLoading" }
  | { type: "treeLoaded"; items: WorkspaceFileEntry[] }
  | { type: "treeFailed"; error: string }
  | { type: "toggleDirectory"; path: string }
  | { type: "fileOpenStart"; path: string }
  | { type: "fileOpenSuccess"; file: WorkspaceFileContent }
  | { type: "fileOpenFailed"; path: string; error: string }
  | { type: "selectFile"; path: string }
  | { type: "closeFile"; path: string }
  | { type: "setBottomPanel"; panel: WorkbenchState["bottomPanel"] };

export const initialWorkbenchState: WorkbenchState = {
  fileTree: { status: "idle", items: [], error: "" },
  expandedDirectories: [""],
  openFiles: [],
  activeFilePath: "",
  bottomPanel: null,
};

export function workbenchReducer(state: WorkbenchState, action: WorkbenchAction): WorkbenchState {
  switch (action.type) {
    case "treeLoading":
      return { ...state, fileTree: { ...state.fileTree, status: "loading", error: "" } };
    case "treeLoaded":
      return { ...state, fileTree: { status: "success", items: action.items, error: "" } };
    case "treeFailed":
      return { ...state, fileTree: { status: "error", items: state.fileTree.items, error: action.error } };
    case "toggleDirectory": {
      const exists = state.expandedDirectories.includes(action.path);
      return {
        ...state,
        expandedDirectories: exists
          ? state.expandedDirectories.filter((path) => path !== action.path)
          : [...state.expandedDirectories, action.path],
      };
    }
    case "fileOpenStart": {
      const existing = state.openFiles.find((file) => file.path === action.path);
      if (existing) {
        return {
          ...state,
          activeFilePath: action.path,
          openFiles: state.openFiles.map((file) =>
            file.path === action.path ? { ...file, loading: true, error: "" } : file,
          ),
        };
      }
      return {
        ...state,
        activeFilePath: action.path,
        openFiles: [
          ...state.openFiles,
          { path: action.path, name: filenameFromPath(action.path), language: "plaintext", content: "", loading: true, error: "" },
        ],
      };
    }
    case "fileOpenSuccess":
      return {
        ...state,
        activeFilePath: action.file.path,
        openFiles: upsertFile(state.openFiles, {
          path: action.file.path,
          name: action.file.name,
          language: action.file.language,
          content: action.file.content,
          loading: false,
          error: "",
        }),
      };
    case "fileOpenFailed":
      return {
        ...state,
        activeFilePath: action.path,
        openFiles: upsertFile(state.openFiles, {
          path: action.path,
          name: filenameFromPath(action.path),
          language: "plaintext",
          content: "",
          loading: false,
          error: action.error,
        }),
      };
    case "selectFile":
      return { ...state, activeFilePath: action.path };
    case "closeFile": {
      const nextFiles = state.openFiles.filter((file) => file.path !== action.path);
      const closingActive = state.activeFilePath === action.path;
      return {
        ...state,
        openFiles: nextFiles,
        activeFilePath: closingActive ? nextFiles[nextFiles.length - 1]?.path || "" : state.activeFilePath,
      };
    }
    case "setBottomPanel":
      return { ...state, bottomPanel: action.panel };
    default:
      return state;
  }
}

function upsertFile(files: OpenFileTab[], next: OpenFileTab) {
  const exists = files.some((file) => file.path === next.path);
  if (!exists) {
    return [...files, next];
  }
  return files.map((file) => (file.path === next.path ? next : file));
}

function filenameFromPath(path: string) {
  return path.split("/").filter(Boolean).pop() || path;
}
