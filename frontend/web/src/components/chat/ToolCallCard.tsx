import { ChevronDown, ChevronRight, Loader2, Wrench } from "lucide-react";
import { useState } from "react";

interface ToolCallCardProps {
  toolName: string;
  status: "running" | "success" | "failed";
  arguments?: unknown;
  output?: string;
}

export function ToolCallCard({ toolName, status, arguments: args, output }: ToolCallCardProps) {
  const [open, setOpen] = useState(false);
  const StatusIcon = status === "running" ? Loader2 : Wrench;

  return (
    <article className="rounded border border-slate-800 bg-black/25 p-3">
      <button type="button" onClick={() => setOpen((value) => !value)} className="flex w-full items-center gap-2 text-left">
        {open ? <ChevronDown className="h-3.5 w-3.5 text-slate-500" /> : <ChevronRight className="h-3.5 w-3.5 text-slate-500" />}
        <StatusIcon className={`h-4 w-4 ${status === "running" ? "animate-spin text-cyan-300" : status === "failed" ? "text-rose-300" : "text-emerald-300"}`} />
        <span className="min-w-0 flex-1 truncate font-mono text-xs font-bold text-slate-200">{toolName}</span>
        <span className="rounded border border-slate-700 px-1.5 py-0.5 font-mono text-[10px] uppercase text-slate-500">{status}</span>
      </button>
      {open ? (
        <div className="mt-3 space-y-2">
          {args !== undefined ? <Block title="Arguments" value={JSON.stringify(args, null, 2)} /> : null}
          {output ? <Block title="Result" value={output} /> : null}
        </div>
      ) : null}
    </article>
  );
}

function Block({ title, value }: { title: string; value: string }) {
  return (
    <div>
      <div className="mb-1 font-mono text-[10px] font-bold uppercase text-slate-600">{title}</div>
      <pre className="custom-scrollbar max-h-48 overflow-auto whitespace-pre-wrap rounded border border-slate-800 bg-slate-950 p-2 font-mono text-[11px] leading-5 text-slate-300">
        {value}
      </pre>
    </div>
  );
}
