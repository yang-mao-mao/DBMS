import type { CatalogResult, HealthResult, QueryResult } from "../types/database";
import type { DatabaseService } from "./DatabaseService";
import { mockCatalog, mockPlan, studentsResult } from "../mocks/mockData";

const delay = (milliseconds: number): Promise<void> =>
  new Promise((resolve) => window.setTimeout(resolve, milliseconds));

export class MockDatabaseService implements DatabaseService {
  async healthCheck(): Promise<HealthResult> {
    await delay(80);
    return { healthy: true, service: "Mock backend", version: "demo" };
  }

  async getCatalog(): Promise<CatalogResult> {
    await delay(120);
    return structuredClone(mockCatalog);
  }

  async execute(sql: string): Promise<QueryResult> {
    await delay(180);
    const statement = sql.trim();
    const upper = statement.toUpperCase();
    if (!statement) return failure("INVALID_ARGUMENT", "SQL statement is empty");
    if (upper.startsWith("CREATE INDEX")) {
      return failure("UNSUPPORTED", "Demo mode does not expose physical indexes; use Real Mode");
    }
    if (upper.startsWith("EXPLAIN")) {
      const formattedPlan = "Project(name)\n  Filter(age > 20)\n    TableScan(table=students)";
      return {
        success: true,
        columns: [{ name: "QUERY PLAN", type: "VARCHAR" }],
        rows: [[formattedPlan]],
        affectedRows: 0,
        elapsedMs: 1,
        message: "Demo plan generated",
        formattedPlan,
        plan: structuredClone(mockPlan),
      };
    }
    if (upper.startsWith("CREATE TABLE")) {
      return successMessage("Table created in demo session.", 1);
    }
    if (upper.startsWith("INSERT INTO")) {
      return successMessage("1 row inserted in demo session.", 1);
    }
    if (upper.startsWith("SELECT NAME") && upper.includes("AGE > 20")) {
      return {
        success: true,
        columns: [{ name: "name", type: "VARCHAR" }],
        rows: [["Alice"], ["Chen"], ["Diana"]],
        affectedRows: 0,
        elapsedMs: 2,
        message: "3 rows returned from Mock backend",
      };
    }
    if (upper.startsWith("SELECT") && upper.includes("FROM STUDENTS")) return studentsResult();
    return failure("UNSUPPORTED", "This statement is not part of the PageCraft Studio demo dataset");
  }
}

const successMessage = (message: string, affectedRows: number): QueryResult => ({
  success: true,
  columns: [],
  rows: [],
  affectedRows,
  elapsedMs: 1,
  message,
});

const failure = (code: string, message: string): QueryResult => ({
  success: false,
  columns: [],
  rows: [],
  affectedRows: 0,
  elapsedMs: 1,
  message,
  error: { code, message },
});
