import type { CatalogResult, HealthResult, QueryResult } from "../types/database";
import type { DatabaseService } from "./DatabaseService";
import { DatabaseServiceError } from "./DatabaseService";
import { parseCatalogResult, parseHealthResult, parseQueryResult } from "./validation";

export class HttpDatabaseService implements DatabaseService {
  private readonly request: typeof fetch;

  constructor(
    private readonly baseUrl: string,
    request?: typeof fetch,
  ) {
    this.request = request ?? globalThis.fetch.bind(globalThis);
  }

  healthCheck(): Promise<HealthResult> {
    return this.get("/api/v1/health", parseHealthResult);
  }

  getCatalog(): Promise<CatalogResult> {
    return this.get("/api/v1/catalog", parseCatalogResult);
  }

  async execute(sql: string): Promise<QueryResult> {
    return this.send("/api/v1/query", { sql }, parseQueryResult);
  }

  private async get<T>(path: string, parse: (value: unknown) => T): Promise<T> {
    return this.perform(path, { headers: { Accept: "application/json" } }, parse);
  }

  private async send<T>(path: string, body: object, parse: (value: unknown) => T): Promise<T> {
    return this.perform(
      path,
      {
        method: "POST",
        headers: { Accept: "application/json", "Content-Type": "application/json" },
        body: JSON.stringify(body),
      },
      parse,
    );
  }

  private async perform<T>(path: string, init: RequestInit, parse: (value: unknown) => T): Promise<T> {
    let response: Response;
    try {
      response = await this.request(`${this.baseUrl.replace(/\/$/, "")}${path}`, init);
    } catch {
      throw new DatabaseServiceError("PageCraft API is unreachable", "NETWORK");
    }
    if (!response.ok) {
      throw new DatabaseServiceError(`PageCraft API returned HTTP ${response.status}`, "HTTP_ERROR");
    }
    let value: unknown;
    try {
      value = await response.json();
    } catch {
      throw new DatabaseServiceError("PageCraft API returned invalid JSON", "INVALID_RESPONSE");
    }
    return parse(value);
  }
}
