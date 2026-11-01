import { useEffect } from "react";
import { ConnectionGate } from "../components/connection/ConnectionGate";
import { AppHeader } from "../components/layout/AppHeader";
import { MainWorkspace } from "../components/layout/MainWorkspace";
import { StatusBar } from "../components/layout/StatusBar";
import { useWorkspaceStore } from "../stores/workspaceStore";

export default function App() {
  const theme = useWorkspaceStore((state) => state.theme);
  useEffect(() => {
    document.documentElement.classList.toggle("dark", theme === "dark");
    document.documentElement.dataset.theme = theme;
  }, [theme]);

  return (
    <div className="flex h-dvh min-h-[520px] flex-col overflow-hidden bg-[var(--background)] text-[var(--text)]">
      <AppHeader />
      <MainWorkspace />
      <StatusBar running={false} />
      <ConnectionGate />
    </div>
  );
}
