import { AlertTriangle, CheckCircle2, Network, ScanSearch, Text } from "lucide-react";
import { useState } from "react";
import type { PlanNode, QueryResult } from "../../types/database";
import { EmptyResult } from "../result/EmptyResult";
import { PlanTree } from "./PlanTree";

interface ExplainViewProps {
  result: QueryResult | null;
}

type AccessPath = "index" | "table" | "unknown";

function containsNode(node: PlanNode | undefined, type: PlanNode["type"]): boolean {
  return node?.type === type || node?.children.some((child) => containsNode(child, type)) === true;
}

function detectAccessPath(result: QueryResult): AccessPath {
  if (containsNode(result.plan, "IndexScan") || /\bIndexScan\b/.test(result.formattedPlan ?? "")) return "index";
  if (containsNode(result.plan, "TableScan") || /\bTableScan\b/.test(result.formattedPlan ?? "")) return "table";
  return "unknown";
}

function AccessPathStatus({ path }: { path: AccessPath }) {
  if (path === "index") {
    return <div className="ml-auto flex items-center gap-1 text-[10px] text-emerald-400"><CheckCircle2 size={11} /> Physical B+ index active</div>;
  }
  if (path === "table") {
    return <div className="ml-auto flex items-center gap-1 text-[10px] text-[var(--text-secondary)]"><ScanSearch size={11} /> Sequential table scan</div>;
  }
  return <div className="ml-auto flex items-center gap-1 text-[10px] text-amber-400"><AlertTriangle size={11} /> Access path unavailable</div>;
}

export function ExplainView({ result }: ExplainViewProps) {
  const [view, setView] = useState<"visual" | "text">("visual");
  if (!result?.plan && !result?.formattedPlan) return <EmptyResult title="No execution plan" description="Run EXPLAIN to inspect the physical plan." />;
  const accessPath = detectAccessPath(result);
  return (
    <div className="flex h-full min-h-0 flex-col">
      <div className="flex h-9 shrink-0 items-center gap-1 border-b border-[var(--border)] px-2">
        <button className={`tab-button compact ${view === "visual" ? "active" : ""}`} onClick={() => setView("visual")}><Network size={12} /> Visual</button>
        <button className={`tab-button compact ${view === "text" ? "active" : ""}`} onClick={() => setView("text")}><Text size={12} /> Text</button>
        <AccessPathStatus path={accessPath} />
      </div>
      <div className="min-h-0 flex-1 overflow-auto p-3">
        {view === "visual" && result.plan ? <PlanTree plan={result.plan} /> : (
          <pre className="h-full whitespace-pre-wrap rounded-lg border border-[var(--border)] bg-[var(--background)] p-4 font-mono text-xs leading-6 text-[var(--text-secondary)]">{result.formattedPlan ?? "Structured plan only"}</pre>
        )}
      </div>
    </div>
  );
}
