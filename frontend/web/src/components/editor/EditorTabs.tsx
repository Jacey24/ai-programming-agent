import { X } from "lucide-react";

import type { OpenFileTab } from "../../types/api";

interface EditorTabsProps {
  files: OpenFileTab[];
  activeFilePath: string;
  onSelectFile: (path: string) => void;
  onCloseFile: (path: string) => void;
}

export function EditorTabs({ files, activeFilePath, onSelectFile, onCloseFile }: EditorTabsProps) {
  if (!files.length) {
    return <div className="h-10 border-b border-slate-800 bg-black/25" />;
  }

  return (
    <div className="custom-scrollbar flex h-10 shrink-0 overflow-x-auto border-b border-slate-800 bg-black/25">
      {files.map((file) => {
        const active = file.path === activeFilePath;
        return (
          <button
            key={file.path}
            type="button"
            onClick={() => onSelectFile(file.path)}
            className={`group flex h-10 max-w-56 shrink-0 items-center gap-2 border-r border-slate-800 px-3 font-mono text-xs ${
              active ? "border-t-2 border-t-cyan-300 bg-[#0b1220] text-slate-100" : "text-slate-500 hover:bg-white/5 hover:text-slate-200"
            }`}
            title={file.path}
          >
            <span className="truncate">{file.name}</span>
            <span
              role="button"
              tabIndex={0}
              onClick={(event) => {
                event.stopPropagation();
                onCloseFile(file.path);
              }}
              onKeyDown={(event) => {
                if (event.key === "Enter" || event.key === " ") {
                  event.preventDefault();
                  event.stopPropagation();
                  onCloseFile(file.path);
                }
              }}
              className="grid h-4 w-4 shrink-0 place-items-center rounded text-slate-600 hover:bg-white/10 hover:text-slate-200"
              title="Close file"
            >
              <X className="h-3 w-3" />
            </span>
          </button>
        );
      })}
    </div>
  );
}
