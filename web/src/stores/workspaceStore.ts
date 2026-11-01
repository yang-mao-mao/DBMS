import { create } from "zustand";
import { persist } from "zustand/middleware";
import type { ExecutionMode, QueryHistoryEntry, QueryResult } from "../types/database";

export type ResultTab = "results" | "messages" | "explain" | "history";

interface WorkspaceState {
  theme: "dark" | "light";
  mode: ExecutionMode;
  sidebarOpen: boolean;
  resultsMaximized: boolean;
  resultTab: ResultTab;
  sql: string;
  lastResult: QueryResult | null;
  history: QueryHistoryEntry[];
  setTheme(theme: "dark" | "light"): void;
  setMode(mode: ExecutionMode): void;
  setSidebarOpen(open: boolean): void;
  setResultsMaximized(maximized: boolean): void;
  setResultTab(tab: ResultTab): void;
  setSql(sql: string): void;
  setLastResult(result: QueryResult | null): void;
  addHistory(entry: QueryHistoryEntry): void;
  removeHistory(id: string): void;
  clearHistory(): void;
}

export const useWorkspaceStore = create<WorkspaceState>()(
  persist(
    (set) => ({
      theme: "dark",
      mode: "real",
      sidebarOpen: true,
      resultsMaximized: false,
      resultTab: "results",
      sql: "SELECT name\nFROM students\nWHERE age > 20;",
      lastResult: null,
      history: [],
      setTheme: (theme) => set({ theme }),
      setMode: (mode) => set({ mode }),
      setSidebarOpen: (sidebarOpen) => set({ sidebarOpen }),
      setResultsMaximized: (resultsMaximized) => set({ resultsMaximized }),
      setResultTab: (resultTab) => set({ resultTab }),
      setSql: (sql) => set({ sql }),
      setLastResult: (lastResult) => set({ lastResult }),
      addHistory: (entry) => set((state) => ({ history: [entry, ...state.history].slice(0, 100) })),
      removeHistory: (id) => set((state) => ({ history: state.history.filter((entry) => entry.id !== id) })),
      clearHistory: () => set({ history: [] }),
    }),
    {
      name: "pagecraft-studio-workspace",
      partialize: (state) => ({
        theme: state.theme,
        sidebarOpen: state.sidebarOpen,
        sql: state.sql,
        history: state.history,
      }),
    },
  ),
);
