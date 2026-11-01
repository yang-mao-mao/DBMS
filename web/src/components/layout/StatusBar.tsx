import { Circle, Clock3, Rows3, ScanSearch, Server } from "lucide-react";
import { useDatabaseService } from "../../app/serviceContext";
import { useWorkspaceStore } from "../../stores/workspaceStore";

interface StatusBarProps {
  running: boolean;
}

export function StatusBar({ running }: StatusBarProps) {
  const result = useWorkspaceStore((state) => state.lastResult);
  const { mode } = useDatabaseService();
  const planKind = result?.formattedPlan?.match(/(IndexScan|TableScan)/)?.[1] ?? "—";
  return (
    <footer className="flex h-6 shrink-0 items-center border-t border-[var(--border)] bg-[var(--surface-elevated)] px-2 text-[10px] text-[var(--muted)]">
      <span className="flex items-center gap-1.5"><Circle size={7} fill={running ? "#F4B942" : "#3FC380"} className={running ? "text-amber-400" : "text-emerald-400"} />{running ? "Running" : "Ready"}</span>
      <span className="status-separator" />
      <span className="flex items-center gap-1"><Rows3 size={10} />{result?.rows.length ?? 0} rows</span>
      <span className="status-separator" />
      <span className="flex items-center gap-1"><Clock3 size={10} />{result?.elapsedMs ?? 0} ms</span>
      <span className="status-separator hidden sm:block" />
      <span className="hidden items-center gap-1 sm:flex"><ScanSearch size={10} />{planKind}</span>
      <span className="ml-auto flex items-center gap-1.5 font-medium"><Server size={10} />{mode === "demo" ? "Mock backend · DEMO MODE" : "PageCraft API · REAL MODE"}</span>
    </footer>
  );
}
