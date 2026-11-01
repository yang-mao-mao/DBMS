import { render, screen } from "@testing-library/react";
import { ResultTable } from "../components/result/ResultTable";
import type { QueryResult } from "../types/database";
import { escapeCsvCell, resultToCsv } from "../utils/export";

const result: QueryResult = {
  success: true,
  columns: [
    { name: "id", type: "INT64" },
    { name: "name", type: "VARCHAR" },
    { name: "note", type: "VARCHAR" },
  ],
  rows: [[1, "Alice", null], [2, "Bob", "active"]],
  affectedRows: 0,
  elapsedMs: 2,
  message: "2 rows",
};

describe("result rendering", () => {
  it("renders dynamic INT and VARCHAR columns", () => {
    render(<ResultTable result={result} />);
    expect(screen.getByText("id")).toBeInTheDocument();
    expect(screen.getByText("INT64")).toBeInTheDocument();
    expect(screen.getAllByText("VARCHAR")).toHaveLength(2);
  });

  it("renders multiple rows and NULL styling", () => {
    render(<ResultTable result={result} />);
    expect(screen.getByText("Alice")).toBeInTheDocument();
    expect(screen.getByText("Bob")).toBeInTheDocument();
    expect(screen.getByText("NULL")).toHaveClass("italic");
  });

  it("shows an empty result set", () => {
    render(<ResultTable result={{ ...result, rows: [] }} />);
    expect(screen.getByText("Empty result set")).toBeInTheDocument();
  });

  it("shows affected rows for a statement without columns", () => {
    render(<ResultTable result={{ ...result, columns: [], rows: [], affectedRows: 1, message: "Table created." }} />);
    expect(screen.getByText("Table created.")).toBeInTheDocument();
    expect(screen.getByText("1 affected row.")).toBeInTheDocument();
  });

  it("escapes quotes, commas and line breaks in CSV", () => {
    expect(escapeCsvCell('a,"b"\nline')).toBe('"a,""b""\nline"');
    expect(resultToCsv([{ name: "value", type: "VARCHAR" }], [['a,b']])).toContain('"a,b"');
  });
});
