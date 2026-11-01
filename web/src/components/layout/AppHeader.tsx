import { CircleHelp, Database, Menu, Moon, PanelLeftClose, Settings, Sun } from "lucide-react";
import { useState } from "react";
import { useDatabaseService } from "../../app/serviceContext";
import { useWorkspaceStore } from "../../stores/workspaceStore";
import { AboutDialog } from "../branding/AboutDialog";
import { PageCraftLogo } from "../branding/PageCraftLogo";

export function AppHeader() {
  const [aboutOpen, setAboutOpen] = useState(false);
  const theme = useWorkspaceStore((state) => state.theme);
  const setTheme = useWorkspaceStore((state) => state.setTheme);
  const sidebarOpen = useWorkspaceStore((state) => state.sidebarOpen);
  const setSidebarOpen = useWorkspaceStore((state) => state.setSidebarOpen);
  const { mode, retryRealMode } = useDatabaseService();

  return (
    <>
      <header className="flex h-14 shrink-0 items-center border-b border-[var(--border)] bg-[var(--surface)] px-3">
        <button className="icon-button mr-2 md:hidden" onClick={() => setSidebarOpen(!sidebarOpen)} aria-label="Toggle catalog"><Menu size={18} /></button>
        <div className="min-w-[225px]"><PageCraftLogo /></div>
        <nav className="hidden h-full items-center gap-1 md:flex" aria-label="Application navigation">
          <button className="nav-button"><Database size={14} /> Database</button>
          <button className="nav-button" onClick={retryRealMode}>Connection</button>
          <button className="nav-button" onClick={() => setTheme(theme === "dark" ? "light" : "dark")}>Theme</button>
        </nav>
        <div className="ml-auto flex items-center gap-2">
          <button
            className={`mode-pill ${mode === "demo" ? "mode-demo" : "mode-real"}`}
            onClick={mode === "demo" ? retryRealMode : undefined}
            title={mode === "demo" ? "Retry real API" : "Connected mode"}
          >
            <span className="status-dot" /> {mode === "demo" ? "DEMO MODE" : "REAL MODE"}
          </button>
          <button className="icon-button hidden sm:grid" onClick={() => setSidebarOpen(!sidebarOpen)} aria-label="Toggle catalog panel"><PanelLeftClose size={17} /></button>
          <button className="icon-button" onClick={() => setTheme(theme === "dark" ? "light" : "dark")} aria-label={`Use ${theme === "dark" ? "light" : "dark"} theme`}>
            {theme === "dark" ? <Sun size={17} /> : <Moon size={17} />}
          </button>
          <button className="icon-button" onClick={() => setAboutOpen(true)} aria-label="About PageCraft Studio"><CircleHelp size={17} /></button>
          <button className="icon-button hidden sm:grid" aria-label="Settings"><Settings size={17} /></button>
        </div>
      </header>
      <AboutDialog open={aboutOpen} onClose={() => setAboutOpen(false)} />
    </>
  );
}
