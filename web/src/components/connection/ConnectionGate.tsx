import { AlertTriangle, LoaderCircle, RefreshCw, ServerOff } from "lucide-react";
import { useQuery } from "@tanstack/react-query";
import { useDatabaseService } from "../../app/serviceContext";

export function ConnectionGate() {
  const { service, mode, useDemoMode } = useDatabaseService();
  const health = useQuery({ queryKey: ["health", mode], queryFn: () => service.healthCheck(), retry: false });
  if (mode === "demo" || health.isSuccess) return null;
  if (health.isLoading) {
    return <div className="fixed inset-0 z-50 grid place-items-center bg-[var(--background)]"><div className="text-center"><LoaderCircle size={28} className="mx-auto animate-spin text-brand" /><div className="mt-4 text-sm font-medium">Connecting to PageCraft DB</div><div className="mt-1 text-xs text-[var(--muted)]">Checking the local API…</div></div></div>;
  }
  return (
    <div className="fixed inset-0 z-50 grid place-items-center bg-black/60 p-4">
      <section role="dialog" aria-modal="true" aria-labelledby="connection-title" className="w-full max-w-md rounded-xl border border-[var(--border)] bg-[var(--panel)] p-6 shadow-panel">
        <div className="grid h-11 w-11 place-items-center rounded-xl bg-amber-400/10 text-amber-400"><ServerOff size={21} /></div>
        <h2 id="connection-title" className="mt-5 text-lg font-semibold">PageCraft API is unavailable</h2>
        <p className="mt-2 text-sm leading-6 text-[var(--text-secondary)]">PageCraft Studio could not reach the local C++ bridge. Demo mode is optional and clearly separated from real database execution.</p>
        <div className="mt-4 flex items-start gap-2 rounded-lg border border-amber-400/20 bg-amber-400/5 p-3 text-xs text-amber-300"><AlertTriangle size={14} className="mt-0.5 shrink-0" />No data will be sent to the internet.</div>
        <div className="mt-6 flex justify-end gap-2">
          <button className="secondary-button" onClick={() => void health.refetch()}><RefreshCw size={14} /> Retry</button>
          <button className="primary-button" onClick={useDemoMode}>Enter demo mode</button>
        </div>
      </section>
    </div>
  );
}
