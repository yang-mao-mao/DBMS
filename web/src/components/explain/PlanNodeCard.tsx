import { Filter, Hash, ScanSearch, SquareStack } from "lucide-react";
import type { PlanNode } from "../../types/database";

interface PlanNodeCardProps {
  node: PlanNode;
}

const icons = { TableScan: ScanSearch, IndexScan: Hash, Filter, Project: SquareStack, Unknown: SquareStack };

export function PlanNodeCard({ node }: PlanNodeCardProps) {
  const Icon = icons[node.type];
  const unavailable = node.type === "IndexScan" && node.details.available === false;
  return (
    <div className={`min-w-44 rounded-lg border bg-[var(--panel)] px-3 py-2 shadow-panel ${unavailable ? "border-amber-400/45" : "border-[var(--border-strong)]"}`}>
      <div className="flex items-center gap-2 text-xs font-semibold"><Icon size={14} className={unavailable ? "text-amber-400" : "text-brand"} />{node.type}</div>
      <div className="mt-2 space-y-1">
        {Object.entries(node.details).map(([key, value]) => (
          <div key={key} className="flex gap-2 text-[9px]"><span className="text-[var(--muted)]">{key}</span><span className="ml-auto max-w-28 truncate text-[var(--text-secondary)]">{Array.isArray(value) ? value.join(", ") : String(value)}</span></div>
        ))}
      </div>
      {unavailable && <div className="mt-2 text-[9px] font-medium text-amber-400">UNAVAILABLE</div>}
    </div>
  );
}
