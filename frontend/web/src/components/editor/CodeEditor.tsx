import Editor from "@monaco-editor/react";
import { FileCode2, Loader2 } from "lucide-react";

import { EditorTabs } from "./EditorTabs";
import type { OpenFileTab } from "../../types/api";

interface CodeEditorProps {
  files: OpenFileTab[];
  activeFilePath: string;
  onSelectFile: (path: string) => void;
  onCloseFile: (path: string) => void;
  className?: string;
}

export function CodeEditor({ files, activeFilePath, onSelectFile, onCloseFile, className = "" }: CodeEditorProps) {
  const activeFile = files.find((file) => file.path === activeFilePath) || files[0] || null;

  return (
    <section className={`min-w-0 flex-1 flex-col bg-[#0b1220] ${className || "flex"}`}>
      <EditorTabs files={files} activeFilePath={activeFile?.path || ""} onSelectFile={onSelectFile} onCloseFile={onCloseFile} />
      <div className="flex h-9 shrink-0 items-center gap-2 border-b border-slate-800 bg-black/15 px-4 font-mono text-[11px] text-slate-500">
        <span>workspace</span>
        {activeFile?.path.split("/").filter(Boolean).map((part) => (
          <span key={part} className="contents">
            <span>/</span>
            <span className="text-slate-300">{part}</span>
          </span>
        ))}
        {activeFile ? (
          <span className="ml-auto rounded border border-slate-700 px-1.5 py-0.5 text-[10px] uppercase text-slate-500">read only</span>
        ) : null}
      </div>
      <div className="min-h-0 flex-1">
        {!activeFile ? (
          <div className="grid h-full place-items-center p-6 text-center">
            <div>
              <FileCode2 className="mx-auto mb-4 h-14 w-14 text-slate-700" />
              <h2 className="font-mono text-base font-bold text-slate-200">No file open</h2>
              <p className="mt-2 max-w-md text-sm leading-6 text-slate-500">Select a real workspace file from the explorer.</p>
            </div>
          </div>
        ) : activeFile.loading ? (
          <div className="grid h-full place-items-center font-mono text-sm text-slate-500">
            <span className="inline-flex items-center gap-2">
              <Loader2 className="h-4 w-4 animate-spin" />
              Loading {activeFile.name}
            </span>
          </div>
        ) : activeFile.error ? (
          <div className="m-4 rounded border border-rose-500/30 bg-rose-500/10 p-4 text-sm leading-6 text-rose-200">
            {activeFile.error}
          </div>
        ) : (
          <Editor
            path={activeFile.path}
            language={activeFile.language}
            value={activeFile.content}
            theme="vs-dark"
            options={{
              readOnly: true,
              automaticLayout: true,
              minimap: { enabled: true },
              fontSize: 14,
              lineNumbers: "on",
              scrollBeyondLastLine: false,
              wordWrap: "off",
              renderWhitespace: "selection",
              folding: true,
            }}
          />
        )}
      </div>
    </section>
  );
}
