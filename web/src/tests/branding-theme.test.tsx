import { fireEvent, render, screen } from "@testing-library/react";
import { PageCraftLogo } from "../components/branding/PageCraftLogo";
import { AboutDialog } from "../components/branding/AboutDialog";
import { AppHeader } from "../components/layout/AppHeader";
import { MockDatabaseService } from "../services/MockDatabaseService";
import { useWorkspaceStore } from "../stores/workspaceStore";
import { renderWithService } from "./testUtils";

describe("PageCraft branding and theme", () => {
  it("shows PageCraft Studio branding", () => {
    render(<PageCraftLogo />);
    expect(screen.getByText("PageCraft Studio")).toBeInTheDocument();
    expect(screen.queryByText("MiniDB")).not.toBeInTheDocument();
  });

  it("describes demo data honestly in About", () => {
    render(<AboutDialog open onClose={vi.fn()} />);
    expect(screen.getByText("PageCraft DB")).toBeInTheDocument();
    expect(screen.getByText(/Mock data for interface preview only/i)).toBeInTheDocument();
  });

  it("switches between dark and light theme state", () => {
    useWorkspaceStore.setState({ theme: "dark" });
    renderWithService(<AppHeader />, new MockDatabaseService(), "demo");
    fireEvent.click(screen.getByLabelText("Use light theme"));
    expect(useWorkspaceStore.getState().theme).toBe("light");
  });

  it("always labels demo execution mode", () => {
    renderWithService(<AppHeader />, new MockDatabaseService(), "demo");
    expect(screen.getByText("DEMO MODE")).toBeInTheDocument();
  });
});
