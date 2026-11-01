import type {
  CatalogResult,
  CellValue,
  DataType,
  HealthResult,
  PlanNode,
  QueryError,
  QueryResult,
  ResultColumn,
  TableMetadata,
} from "../types/database";
import { DatabaseServiceError } from "./DatabaseService";

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const isDataType = (value: unknown): value is DataType =>
  value === "INT" || value === "INT64" || value === "VARCHAR" || value === "UNKNOWN";

const isCellValue = (value: unknown): value is CellValue =>
  value === null || typeof value === "string" || (typeof value === "number" && Number.isFinite(value));

const parseColumn = (value: unknown): ResultColumn => {
  if (!isRecord(value) || typeof value.name !== "string" || !isDataType(value.type)) {
    throw invalidResponse("Invalid result column");
  }
  return { name: value.name, type: value.type };
};

const parseError = (value: unknown): QueryError | undefined => {
  if (value === undefined) return undefined;
  if (!isRecord(value) || typeof value.code !== "string" || typeof value.message !== "string") {
    throw invalidResponse("Invalid query error");
  }
  const error: QueryError = { code: value.code, message: value.message };
  if (typeof value.line === "number") error.line = value.line;
  if (typeof value.column === "number") error.column = value.column;
  return error;
};

const parsePlanNode = (value: unknown): PlanNode => {
  if (!isRecord(value) || typeof value.id !== "string" || typeof value.type !== "string") {
    throw invalidResponse("Invalid plan node");
  }
  const allowed = ["TableScan", "IndexScan", "Filter", "Project", "Unknown"];
  if (!allowed.includes(value.type) || !isRecord(value.details) || !Array.isArray(value.children)) {
    throw invalidResponse("Invalid plan node content");
  }
  const details: PlanNode["details"] = {};
  for (const [key, detail] of Object.entries(value.details)) {
    if (
      typeof detail === "string" ||
      typeof detail === "number" ||
      typeof detail === "boolean" ||
      (Array.isArray(detail) && detail.every((item) => typeof item === "string"))
    ) {
      details[key] = detail;
    } else {
      throw invalidResponse("Invalid plan node detail");
    }
  }
  return {
    id: value.id,
    type: value.type as PlanNode["type"],
    details,
    children: value.children.map(parsePlanNode),
  };
};

const invalidResponse = (message: string): DatabaseServiceError =>
  new DatabaseServiceError(message, "INVALID_RESPONSE");

export const parseQueryResult = (value: unknown): QueryResult => {
  if (!isRecord(value)) throw invalidResponse("Response is not an object");
  if (
    typeof value.success !== "boolean" ||
    !Array.isArray(value.columns) ||
    !Array.isArray(value.rows) ||
    typeof value.affectedRows !== "number" ||
    typeof value.elapsedMs !== "number" ||
    typeof value.message !== "string"
  ) {
    throw invalidResponse("Query response is missing required fields");
  }
  const columns = value.columns.map(parseColumn);
  const rows = value.rows.map((row) => {
    if (!Array.isArray(row) || !row.every(isCellValue)) throw invalidResponse("Invalid result row");
    if (row.length !== columns.length) throw invalidResponse("Result row width does not match columns");
    return row;
  });
  const result: QueryResult = {
    success: value.success,
    columns,
    rows,
    affectedRows: value.affectedRows,
    elapsedMs: value.elapsedMs,
    message: value.message,
  };
  const error = parseError(value.error);
  if (error) result.error = error;
  if (typeof value.formattedPlan === "string") result.formattedPlan = value.formattedPlan;
  if (value.plan !== undefined) result.plan = parsePlanNode(value.plan);
  return result;
};

export const parseHealthResult = (value: unknown): HealthResult => {
  if (!isRecord(value) || typeof value.healthy !== "boolean" || typeof value.service !== "string") {
    throw invalidResponse("Invalid health response");
  }
  const result: HealthResult = { healthy: value.healthy, service: value.service };
  if (typeof value.version === "string") result.version = value.version;
  return result;
};

const parseTable = (value: unknown): TableMetadata => {
  if (!isRecord(value) || typeof value.id !== "number" || typeof value.name !== "string" || !Array.isArray(value.columns) || !Array.isArray(value.indexes)) {
    throw invalidResponse("Invalid table metadata");
  }
  return {
    id: value.id,
    name: value.name,
    columns: value.columns.map((column) => {
      if (!isRecord(column) || typeof column.name !== "string" || !isDataType(column.type)) {
        throw invalidResponse("Invalid column metadata");
      }
      return {
        name: column.name,
        type: column.type,
        ...(typeof column.maxLength === "number" ? { maxLength: column.maxLength } : {}),
        ...(typeof column.nullable === "boolean" ? { nullable: column.nullable } : {}),
      };
    }),
    indexes: value.indexes.map((index) => {
      if (!isRecord(index) || typeof index.name !== "string" || !Array.isArray(index.columns) || !index.columns.every((item) => typeof item === "string")) {
        throw invalidResponse("Invalid index metadata");
      }
      const states = ["available", "unsupported", "building", "unknown"];
      if (typeof index.state !== "string" || !states.includes(index.state)) throw invalidResponse("Invalid index state");
      return { name: index.name, columns: index.columns, state: index.state as TableMetadata["indexes"][number]["state"] };
    }),
  };
};

export const parseCatalogResult = (value: unknown): CatalogResult => {
  if (!isRecord(value) || !Array.isArray(value.tables)) throw invalidResponse("Invalid catalog response");
  return {
    tables: value.tables.map(parseTable),
    ...(typeof value.unavailableReason === "string" ? { unavailableReason: value.unavailableReason } : {}),
  };
};
