import { FileDiff } from "lucide-react";

interface FileChangeCardProps {
  path: string;
  changeType?: string;
  toolName?: string;
  onOpenFile: (path: string) => void;
}

export function FileChangeCard({ path, changeType = "modified", toolName, onOpenFile }: FileChangeCardProps) {
  return (
    <button
      type="button"
      onClick={() => onOpenFile(path)}
      className="flex w-full items-start gap-3 rounded border border-cyan-400/25 bg-cyan-400/10 p-3 text-left hover:border-cyan-300/60"
    >
      <FileDiff className="mt-0.5 h-4 w-4 shrink-0 text-cyan-300" />
      <div className="min-w-0 flex-1">
        <div className="truncate font-mono text-xs font-bold text-cyan-100">
          {labelForChange(changeType)} {path}
        </div>
        <div className="mt-1 truncate font-mono text-[10px] text-cyan-200/60">
          {toolName || "file change"} · click to open
        </div>
      </div>
    </button>
  );
}

function labelForChange(changeType: string) {
  if (changeType === "created") return "Created";
  if (changeType === "deleted") return "Deleted";
  if (changeType === "moved") return "Moved";
  return "Modified";
}
