import { fireEvent, screen } from "@testing-library/react";
import { CatalogTree } from "../components/catalog/CatalogTree";
import { ExplainView } from "../components/explain/ExplainView";
import { QueryHistory } from "../components/history/QueryHistory";
import { MockDatabaseService } from "../services/MockDatabaseService";
import { mockPlan } from "../mocks/mockData";
import { useWorkspaceStore } from "../stores/workspaceStore";
import type { QueryResult } from "../types/database";
import { renderWithService } from "./testUtils";

const explainResult: QueryResult = {
  success: true,
  columns: [{ name: "QUERY PLAN", type: "VARCHAR" }],
  rows: [["Project(name)"]],
  affectedRows: 0,
  elapsedMs: 1,
  message: "plan",
  formattedPlan: "Project(name)\n  Filter(age > 20)\n    TableScan(table=students)",
  plan: mockPlan,
};

describe("catalog, history and explain", () => {
  it("renders catalog tables and columns", async () => {
    renderWithService(<CatalogTree />, new MockDatabaseService(), "demo");
    expect(await screen.findByText("students")).toBeInTheDocument();
    expect(screen.getByText("id")).toBeInTheDocument();
    expect(screen.getAllByText("INT64").length).toBeGreaterThan(0);
  });

  it("generates SELECT star from catalog", async () => {
    renderWithService(<CatalogTree />, new MockDatabaseService(), "demo");
    const items = await screen.findAllByText("Generate SELECT *");
    const first = items.at(0);
    if (!first) throw new Error("Expected a generated SELECT action");
    fireEvent.click(first);
    expect(useWorkspaceStore.getState().sql).toBe("SELECT * FROM students;");
  });

  it("renders formatted EXPLAIN text", () => {
    renderWithService(<ExplainView result={explainResult} />, new MockDatabaseService(), "demo");
    expect(screen.getByText("Sequential table scan")).toBeInTheDocument();
    fireEvent.click(screen.getByText("Text"));
    expect(screen.getByText(/TableScan\(table=students\)/)).toBeInTheDocument();
  });

  it("reports an active physical index for IndexScan plans", () => {
    renderWithService(
      <ExplainView result={{ ...explainResult, plan: undefined, formattedPlan: "Project(name)\n  IndexScan(table=students, index=idx_id)" }} />,
      new MockDatabaseService(),
      "real",
    );
    expect(screen.getByText("Physical B+ index active")).toBeInTheDocument();
    expect(screen.queryByText("Physical index unavailable")).not.toBeInTheDocument();
  });

  it("renders a structured plan tree", () => {
    renderWithService(<ExplainView result={explainResult} />, new MockDatabaseService(), "demo");
    expect(screen.getByText("Project")).toBeInTheDocument();
    expect(screen.getByText("TableScan")).toBeInTheDocument();
  });

  it("searches, reruns and clears query history", () => {
    useWorkspaceStore.setState({ history: [{
      id: "1", sql: "SELECT * FROM students;", executedAt: new Date().toISOString(), success: true,
      rowCount: 2, affectedRows: 0, elapsedMs: 3, mode: "demo",
    }] });
    const run = vi.fn();
    renderWithService(<QueryHistory onRun={run} />, new MockDatabaseService(), "demo");
    expect(screen.getByText("SELECT * FROM students;")).toBeInTheDocument();
    fireEvent.click(screen.getByLabelText("Run again"));
    expect(run).toHaveBeenCalledWith("SELECT * FROM students;");
    fireEvent.click(screen.getByLabelText("Clear history"));
    expect(screen.getByText("No matching query history.")).toBeInTheDocument();
  });
});
