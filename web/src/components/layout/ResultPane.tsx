import { History, Maximize2, MessageSquareText, Minimize2, Network, TableProperties } from "lucide-react";
import { useWorkspaceStore, type ResultTab } from "../../stores/workspaceStore";
import { ExplainView } from "../explain/ExplainView";
import { QueryHistory } from "../history/QueryHistory";
import { MessagePanel } from "../result/MessagePanel";
import { ResultTable } from "../result/ResultTable";

interface ResultPaneProps {
  running: boolean;
  onRun(sql: string): void;
}

const tabs: { id: ResultTab; label: string; icon: typeof TableProperties }[] = [
  { id: "results", label: "Results", icon: TableProperties },
  { id: "messages", label: "Messages", icon: MessageSquareText },
  { id: "explain", label: "Explain", icon: Network },
  { id: "history", label: "History", icon: History },
];

export function ResultPane({ running, onRun }: ResultPaneProps) {
  const active = useWorkspaceStore((state) => state.resultTab);
  const setActive = useWorkspaceStore((state) => state.setResultTab);
  const result = useWorkspaceStore((state) => state.lastResult);
  const maximized = useWorkspaceStore((state) => state.resultsMaximized);
  const setMaximized = useWorkspaceStore((state) => state.setResultsMaximized);

  return (
    <section className="flex h-full min-h-0 flex-col bg-[var(--panel)]" aria-label="Query output">
      <div className="flex h-10 shrink-0 items-center border-b border-[var(--border)] bg-[var(--surface)] px-2">
        {tabs.map((tab) => {
          const Icon = tab.icon;
          return <button key={tab.id} className={`tab-button ${active === tab.id ? "active" : ""}`} onClick={() => setActive(tab.id)}><Icon size={13} />{tab.label}{tab.id === "results" && result?.rows.length ? <span className="tab-count">{result.rows.length}</span> : null}</button>;
        })}
        <button className="icon-button ml-auto h-7 w-7" onClick={() => setMaximized(!maximized)} aria-label={maximized ? "Restore result panel" : "Maximize result panel"}>{maximized ? <Minimize2 size={14} /> : <Maximize2 size={14} />}</button>
      </div>
      <div className="min-h-0 flex-1">
        {active === "results" && <ResultTable result={result} />}
        {active === "messages" && <MessagePanel result={result} loading={running} />}
        {active === "explain" && <ExplainView result={result} />}
        {active === "history" && <QueryHistory onRun={onRun} />}
      </div>
    </section>
  );
}
