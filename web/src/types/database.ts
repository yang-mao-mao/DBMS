export type ExecutionMode = "real" | "demo";
export type DataType = "INT" | "INT64" | "VARCHAR" | "UNKNOWN";
export type CellValue = number | string | null;

export interface ResultColumn {
  name: string;
  type: DataType;
}

export interface QueryError {
  code: string;
  message: string;
  line?: number;
  column?: number;
}

export type PlanNodeType = "TableScan" | "IndexScan" | "Filter" | "Project" | "Unknown";

export interface PlanNode {
  id: string;
  type: PlanNodeType;
  details: Record<string, string | number | boolean | string[]>;
  children: PlanNode[];
}

export interface QueryResult {
  success: boolean;
  columns: ResultColumn[];
  rows: CellValue[][];
  affectedRows: number;
  elapsedMs: number;
  message: string;
  error?: QueryError;
  formattedPlan?: string;
  plan?: PlanNode;
}

export interface ColumnDefinition {
  name: string;
  type: DataType;
  maxLength?: number;
  nullable?: boolean;
}

export interface IndexMetadata {
  name: string;
  columns: string[];
  state: "available" | "unsupported" | "building" | "unknown";
}

export interface TableMetadata {
  id: number;
  name: string;
  columns: ColumnDefinition[];
  indexes: IndexMetadata[];
}

export interface CatalogResult {
  tables: TableMetadata[];
  unavailableReason?: string;
}

export interface HealthResult {
  healthy: boolean;
  service: string;
  version?: string;
}

export interface QueryHistoryEntry {
  id: string;
  sql: string;
  executedAt: string;
  success: boolean;
  rowCount: number;
  affectedRows: number;
  elapsedMs: number;
  errorSummary?: string;
  mode: ExecutionMode;
}
