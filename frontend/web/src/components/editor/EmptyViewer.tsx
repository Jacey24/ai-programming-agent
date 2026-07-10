import { Braces, FileSearch } from "lucide-react";

interface EmptyViewerProps {
  title: string;
  text: string;
  endpoint: string;
}

export function EmptyViewer({ title, text, endpoint }: EmptyViewerProps) {
  return (
    <div className="grid min-h-[420px] flex-1 place-items-center p-6">
      <div className="max-w-xl text-center">
        <FileSearch className="mx-auto mb-4 h-14 w-14 text-slate-700" />
        <h2 className="font-mono text-base font-bold text-slate-200">{title}</h2>
        <p className="mt-3 text-sm leading-6 text-slate-500">{text}</p>
        <div className="mt-5 inline-flex max-w-full items-center gap-2 rounded-lg border border-slate-800 bg-slate-950 px-3 py-2 font-mono text-xs text-cyan-300">
          <Braces className="h-3.5 w-3.5 shrink-0" />
          <span className="truncate">{endpoint}</span>
        </div>
      </div>
    </div>
  );
}
