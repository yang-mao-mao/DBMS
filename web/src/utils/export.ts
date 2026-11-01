import type { CellValue, ResultColumn } from "../types/database";

export const escapeCsvCell = (value: CellValue): string => {
  if (value === null) return "";
  const text = String(value);
  return /[",\r\n]/.test(text) ? `"${text.replaceAll('"', '""')}"` : text;
};

export const resultToCsv = (columns: ResultColumn[], rows: CellValue[][]): string =>
  [
    columns.map((column) => escapeCsvCell(column.name)).join(","),
    ...rows.map((row) => row.map(escapeCsvCell).join(",")),
  ].join("\r\n");

export const resultToTsv = (columns: ResultColumn[], rows: CellValue[][]): string =>
  [columns.map((column) => column.name).join("\t"), ...rows.map((row) => row.map((value) => value ?? "").join("\t"))].join("\n");

export const downloadCsv = (filename: string, csv: string): void => {
  const blob = new Blob(["\uFEFF", csv], { type: "text/csv;charset=utf-8" });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = filename;
  anchor.click();
  URL.revokeObjectURL(url);
};
