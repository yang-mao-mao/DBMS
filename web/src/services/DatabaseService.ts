import type { CatalogResult, HealthResult, QueryResult } from "../types/database";

export interface DatabaseService {
  healthCheck(): Promise<HealthResult>;
  execute(sql: string): Promise<QueryResult>;
  getCatalog(): Promise<CatalogResult>;
}

export class DatabaseServiceError extends Error {
  constructor(
    message: string,
    readonly code: "NETWORK" | "INVALID_RESPONSE" | "HTTP_ERROR",
  ) {
    super(message);
    this.name = "DatabaseServiceError";
  }
}
