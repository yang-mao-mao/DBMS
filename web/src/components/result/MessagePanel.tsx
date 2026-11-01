import { AlertCircle, CheckCircle2, Clock3, LoaderCircle } from "lucide-react";
import type { QueryResult } from "../../types/database";

interface MessagePanelProps {
  result: QueryResult | null;
  loading: boolean;
}

export function MessagePanel({ result, loading }: MessagePanelProps) {
  if (loading) return <div className="empty-inline h-full justify-center"><LoaderCircle size={16} className="animate-spin" /> Executing query…</div>;
  if (!result) return <div className="empty-inline h-full justify-center">Messages from PageCraft DB will appear here.</div>;
  return (
    <div className="h-full overflow-auto p-4 text-sm">
      <div className={`flex items-start gap-3 rounded-lg border p-4 ${result.success ? "border-emerald-400/20 bg-emerald-400/5" : "border-red-400/20 bg-red-400/5"}`}>
        {result.success ? <CheckCircle2 size={18} className="mt-0.5 text-emerald-400" /> : <AlertCircle size={18} className="mt-0.5 text-red-400" />}
        <div>
          <div className="font-medium">{result.success ? "Statement completed" : result.error?.code ?? "Query failed"}</div>
          <div className="mt-1 text-xs leading-5 text-[var(--text-secondary)]">{result.error?.message ?? result.message}</div>
          <div className="mt-3 flex items-center gap-1 text-[10px] text-[var(--muted)]"><Clock3 size={11} /> {result.elapsedMs} ms · {result.affectedRows} affected</div>
        </div>
      </div>
    </div>
  );
}
