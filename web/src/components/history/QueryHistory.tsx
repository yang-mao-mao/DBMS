import { CheckCircle2, Copy, Play, Search, Trash2, XCircle } from "lucide-react";
import { useState } from "react";
import { useWorkspaceStore } from "../../stores/workspaceStore";

interface QueryHistoryProps {
  onRun(sql: string): void;
}

export function QueryHistory({ onRun }: QueryHistoryProps) {
  const history = useWorkspaceStore((state) => state.history);
  const removeHistory = useWorkspaceStore((state) => state.removeHistory);
  const clearHistory = useWorkspaceStore((state) => state.clearHistory);
  const [search, setSearch] = useState("");
  const [filter, setFilter] = useState<"all" | "success" | "failed">("all");
  const filtered = history.filter((entry) =>
    entry.sql.toLowerCase().includes(search.toLowerCase()) &&
    (filter === "all" || (filter === "success" ? entry.success : !entry.success)),
  );

  return (
    <div className="flex h-full min-h-0 flex-col">
      <div className="flex h-10 shrink-0 items-center gap-2 border-b border-[var(--border)] px-2">
        <div className="relative max-w-60 flex-1"><Search size={12} className="absolute left-2 top-2 text-[var(--muted)]" /><input className="h-7 w-full rounded border border-[var(--border)] bg-[var(--background)] pl-7 pr-2 text-xs outline-none focus:border-brand" value={search} onChange={(event) => setSearch(event.target.value)} placeholder="Search history" aria-label="Search history" /></div>
        {(["all", "success", "failed"] as const).map((value) => <button key={value} className={`filter-chip ${filter === value ? "active" : ""}`} onClick={() => setFilter(value)}>{value}</button>)}
        <button className="icon-button ml-auto h-7 w-7" onClick={clearHistory} aria-label="Clear history"><Trash2 size={13} /></button>
      </div>
      <div className="min-h-0 flex-1 overflow-auto">
        {filtered.length === 0 && <div className="empty-inline h-full justify-center">No matching query history.</div>}
        {filtered.map((entry) => (
          <article key={entry.id} className="group grid grid-cols-[24px_minmax(0,1fr)_auto] gap-2 border-b border-[var(--border)] px-3 py-2.5 hover:bg-[var(--hover)]">
            {entry.success ? <CheckCircle2 size={14} className="mt-0.5 text-emerald-400" /> : <XCircle size={14} className="mt-0.5 text-red-400" />}
            <div className="min-w-0"><code className="block truncate font-mono text-xs text-[var(--text-secondary)]">{entry.sql}</code><div className="mt-1 text-[9px] text-[var(--muted)]">{new Date(entry.executedAt).toLocaleString()} · {entry.elapsedMs} ms · {entry.mode.toUpperCase()}</div></div>
            <div className="flex items-center gap-1 opacity-0 group-hover:opacity-100">
              <button className="icon-button h-6 w-6" onClick={() => onRun(entry.sql)} aria-label="Run again"><Play size={12} /></button>
              <button className="icon-button h-6 w-6" onClick={() => void navigator.clipboard.writeText(entry.sql)} aria-label="Copy query"><Copy size={12} /></button>
              <button className="icon-button h-6 w-6" onClick={() => removeHistory(entry.id)} aria-label="Delete history entry"><Trash2 size={12} /></button>
            </div>
          </article>
        ))}
      </div>
    </div>
  );
}
