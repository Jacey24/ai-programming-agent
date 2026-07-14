import { FolderOpen, RefreshCw } from "lucide-react";
import { useState } from "react";

import { FileTree } from "./FileTree";
import type { WorkbenchState } from "../../store/workbenchReducer";
import type { WorkspaceRecord } from "../../types/api";

interface FileExplorerProps {
  workspace: WorkspaceRecord | null;
  tree: WorkbenchState["fileTree"];
  expandedDirectories: string[];
  activeFilePath: string;
  onChooseWorkspace: () => Promise<unknown>;
  onRefresh: () => void;
  onToggleDirectory: (path: string) => void;
  onOpenFile: (path: string) => void;
}

export function FileExplorer({
  workspace,
  tree,
  expandedDirectories,
  activeFilePath,
  onChooseWorkspace,
  onRefresh,
  onToggleDirectory,
  onOpenFile,
}: FileExplorerProps) {
  const [selectingDirectory, setSelectingDirectory] = useState(false);
  const [directoryError, setDirectoryError] = useState("");

  const chooseWorkspace = async () => {
    setSelectingDirectory(true);
    setDirectoryError("");
    try {
      await onChooseWorkspace();
    } catch (error) {
      setDirectoryError(error instanceof Error ? error.message : String(error));
    } finally {
      setSelectingDirectory(false);
    }
  };

  return (
    <aside className="hidden w-72 shrink-0 flex-col border-r border-slate-800 bg-[#0d1421]/95 font-mono lg:flex">
      <div className="border-b border-slate-800 px-4 py-3">
        <div className="flex items-center justify-between gap-2">
          <div className="min-w-0">
            <div className="truncate text-xs font-bold uppercase text-slate-300">Explorer</div>
            <div className="mt-1 truncate text-[11px] text-slate-500">
              {workspace?.path || "/workspace"}
            </div>
            {directoryError ? <div className="mt-1 truncate text-[10px] text-rose-300">{directoryError}</div> : null}
          </div>
          <div className="flex shrink-0 gap-1">
            <button
              type="button"
              onClick={() => void chooseWorkspace()}
              disabled={selectingDirectory}
              className="grid h-7 w-7 place-items-center rounded border border-slate-700 text-slate-500 hover:border-cyan-400/50 hover:text-cyan-200 disabled:cursor-wait disabled:opacity-60"
              title="选择本机目录作为工作区"
            >
              <FolderOpen className={`h-3.5 w-3.5 ${selectingDirectory ? "animate-pulse" : ""}`} />
            </button>
            <button
              type="button"
              onClick={onRefresh}
              className="grid h-7 w-7 place-items-center rounded border border-slate-700 text-slate-500 hover:border-cyan-400/50 hover:text-cyan-200"
              title="Refresh workspace tree"
            >
              <RefreshCw className={`h-3.5 w-3.5 ${tree.status === "loading" ? "animate-spin" : ""}`} />
            </button>
          </div>
        </div>
      </div>

      <div className="custom-scrollbar min-h-0 flex-1 overflow-auto">
        {tree.status === "error" ? (
          <div className="m-3 rounded border border-rose-500/30 bg-rose-500/10 p-3 text-xs leading-5 text-rose-200">
            {tree.error}
          </div>
        ) : tree.status === "loading" && !tree.items.length ? (
          <div className="p-4 text-xs text-slate-500">Loading workspace tree.</div>
        ) : (
          <FileTree
            items={tree.items}
            expandedDirectories={expandedDirectories}
            activeFilePath={activeFilePath}
            onToggleDirectory={onToggleDirectory}
            onOpenFile={onOpenFile}
          />
        )}
      </div>
    </aside>
  );
}
