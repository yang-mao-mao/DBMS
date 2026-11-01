import { useEffect, useState, type PointerEvent as ReactPointerEvent } from "react";
import { useExecuteQuery } from "../../hooks/useExecuteQuery";
import { useWorkspaceStore } from "../../stores/workspaceStore";
import { AppSidebar } from "./AppSidebar";
import { ResultPane } from "./ResultPane";
import { SqlEditor } from "../sql/SqlEditor";

export function MainWorkspace() {
  const [sidebarWidth, setSidebarWidth] = useState(260);
  const [resultHeight, setResultHeight] = useState(330);
  const sidebarOpen = useWorkspaceStore((state) => state.sidebarOpen);
  const maximized = useWorkspaceStore((state) => state.resultsMaximized);
  const execute = useExecuteQuery();

  useEffect(() => {
    const collapse = (): void => {
      if (window.innerWidth < 760) useWorkspaceStore.getState().setSidebarOpen(false);
    };
    collapse();
    window.addEventListener("resize", collapse);
    return () => window.removeEventListener("resize", collapse);
  }, []);

  const beginSidebarResize = (event: ReactPointerEvent): void => {
    const startX = event.clientX;
    const startWidth = sidebarWidth;
    const move = (pointer: PointerEvent): void => setSidebarWidth(Math.min(430, Math.max(190, startWidth + pointer.clientX - startX)));
    const stop = (): void => { window.removeEventListener("pointermove", move); window.removeEventListener("pointerup", stop); };
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", stop);
  };

  const beginResultResize = (event: ReactPointerEvent): void => {
    const startY = event.clientY;
    const startHeight = resultHeight;
    const move = (pointer: PointerEvent): void => setResultHeight(Math.min(window.innerHeight - 180, Math.max(170, startHeight + startY - pointer.clientY)));
    const stop = (): void => { window.removeEventListener("pointermove", move); window.removeEventListener("pointerup", stop); };
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", stop);
  };

  return (
    <main className="relative flex min-h-0 flex-1 overflow-hidden">
      {sidebarOpen && (
        <>
          <div className="absolute inset-y-0 left-0 z-20 md:relative" style={{ width: sidebarWidth }}><AppSidebar /></div>
          <div className="relative z-30 hidden w-1 cursor-col-resize bg-transparent hover:bg-brand md:block" onPointerDown={beginSidebarResize} />
          <button className="absolute inset-0 z-10 bg-black/35 md:hidden" onClick={() => useWorkspaceStore.getState().setSidebarOpen(false)} aria-label="Close catalog overlay" />
        </>
      )}
      <div className="flex min-w-0 flex-1 flex-col">
        {!maximized && <div className="min-h-0 flex-1"><SqlEditor running={execute.isPending} onExecute={(sql) => execute.mutate(sql)} /></div>}
        {!maximized && <div className="h-1 cursor-row-resize bg-[var(--border)] hover:bg-brand" onPointerDown={beginResultResize} />}
        <div className={maximized ? "min-h-0 flex-1" : "shrink-0"} style={maximized ? undefined : { height: resultHeight }}>
          <ResultPane running={execute.isPending} onRun={(sql) => execute.mutate(sql)} />
        </div>
      </div>
    </main>
  );
}
