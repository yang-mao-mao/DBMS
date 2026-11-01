import type { CatalogResult, PlanNode, QueryResult } from "../types/database";

export const mockCatalog: CatalogResult = {
  tables: [
    {
      id: 1,
      name: "students",
      columns: [
        { name: "id", type: "INT64", nullable: false },
        { name: "name", type: "VARCHAR", maxLength: 32, nullable: false },
        { name: "age", type: "INT64", nullable: false },
      ],
      indexes: [{ name: "idx_id", columns: ["id"], state: "unsupported" }],
    },
    {
      id: 2,
      name: "courses",
      columns: [
        { name: "code", type: "VARCHAR", maxLength: 16, nullable: false },
        { name: "title", type: "VARCHAR", maxLength: 64, nullable: false },
      ],
      indexes: [],
    },
  ],
};

export const mockPlan: PlanNode = {
  id: "project-1",
  type: "Project",
  details: { fields: ["name"] },
  children: [
    {
      id: "filter-1",
      type: "Filter",
      details: { predicate: "age > 20" },
      children: [
        {
          id: "scan-1",
          type: "TableScan",
          details: { table: "students", physicalIndexAvailable: false },
          children: [],
        },
      ],
    },
  ],
};

export const studentsResult = (elapsedMs = 2): QueryResult => ({
  success: true,
  columns: [
    { name: "id", type: "INT64" },
    { name: "name", type: "VARCHAR" },
    { name: "age", type: "INT64" },
  ],
  rows: [
    [1, "Alice", 21],
    [2, "Bob", 19],
    [3, "Chen", 23],
    [4, "Diana", 22],
  ],
  affectedRows: 0,
  elapsedMs,
  message: "4 rows returned from Mock backend",
});
