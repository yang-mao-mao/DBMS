import { fireEvent, screen } from "@testing-library/react";
import { ConnectionGate } from "../components/connection/ConnectionGate";
import { SqlEditor } from "../components/sql/SqlEditor";
import type { DatabaseService } from "../services/DatabaseService";
import { MockDatabaseService } from "../services/MockDatabaseService";
import { useWorkspaceStore } from "../stores/workspaceStore";
import { renderWithService } from "./testUtils";

const offlineService: DatabaseService = {
  healthCheck: () => Promise.reject(new Error("offline")),
  execute: () => Promise.reject(new Error("offline")),
  getCatalog: () => Promise.reject(new Error("offline")),
};

describe("connection and editor behavior", () => {
  it("does not silently enter demo mode when the API fails", async () => {
    renderWithService(<ConnectionGate />, offlineService, "real");
    expect(await screen.findByText("PageCraft API is unavailable")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Enter demo mode" })).toBeInTheDocument();
    expect(useWorkspaceStore.getState().mode).toBe("real");
  });

  it("shows loading state while health check is pending", () => {
    const pending: DatabaseService = { ...offlineService, healthCheck: () => new Promise(() => undefined) };
    renderWithService(<ConnectionGate />, pending, "real");
    expect(screen.getByText("Connecting to PageCraft DB")).toBeInTheDocument();
  });

  it("executes from the Run button", () => {
    useWorkspaceStore.setState({ sql: "SELECT * FROM students;" });
    const execute = vi.fn();
    renderWithService(<SqlEditor running={false} onExecute={execute} />, new MockDatabaseService(), "demo");
    fireEvent.click(screen.getByRole("button", { name: "Run" }));
    expect(execute).toHaveBeenCalledWith("SELECT * FROM students;");
  });

  it("supports Ctrl+Enter in the SQL editor", () => {
    useWorkspaceStore.setState({ sql: "SELECT name FROM students;" });
    const execute = vi.fn();
    const { container } = renderWithService(<SqlEditor running={false} onExecute={execute} />, new MockDatabaseService(), "demo");
    const editor = container.querySelector(".cm-content");
    expect(editor).not.toBeNull();
    if (!editor) throw new Error("CodeMirror editor was not rendered");
    fireEvent.keyDown(editor, { key: "Enter", code: "Enter", ctrlKey: true });
    expect(execute).toHaveBeenCalledWith("SELECT name FROM students;");
  });
});
