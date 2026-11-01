import {
  createColumnHelper,
  flexRender,
  getCoreRowModel,
  getPaginationRowModel,
  useReactTable,
  type ColumnDef,
} from "@tanstack/react-table";
import { useMemo } from "react";
import type { CellValue, QueryResult } from "../../types/database";
import { downloadCsv, resultToCsv, resultToTsv } from "../../utils/export";
import { EmptyResult } from "./EmptyResult";
import { ResultToolbar } from "./ResultToolbar";

interface ResultTableProps {
  result: QueryResult | null;
}

const columnHelper = createColumnHelper<CellValue[]>();

export function ResultTable({ result }: ResultTableProps) {
  const columns = useMemo<ColumnDef<CellValue[], CellValue>[]>(() => {
    if (!result) return [];
    return result.columns.map((column, index) =>
      columnHelper.accessor((row) => row[index] ?? null, {
        id: `${index}-${column.name}`,
        header: () => (
          <div className="flex items-center gap-2">
            <span>{column.name}</span>
            <span className="text-[9px] font-normal text-[var(--muted)]">{column.type}</span>
          </div>
        ),
        cell: (info) => {
          const value = info.getValue();
          return (
            <button
              className={`block w-full truncate text-left ${value === null ? "italic text-[var(--muted)]" : ""}`}
              onClick={() => void navigator.clipboard.writeText(value === null ? "NULL" : String(value))}
              title={value === null ? "NULL" : String(value)}
            >{value === null ? "NULL" : String(value)}</button>
          );
        },
        size: Math.max(120, column.name.length * 12 + 70),
      }),
    );
  }, [result]);

  const table = useReactTable({
    data: result?.rows ?? [],
    columns,
    getCoreRowModel: getCoreRowModel(),
    getPaginationRowModel: getPaginationRowModel(),
    columnResizeMode: "onChange",
    initialState: { pagination: { pageSize: 50 } },
  });

  if (!result) return <EmptyResult />;
  if (result.success && result.columns.length === 0) {
    return <EmptyResult title={result.message || "Statement completed"} description={`${result.affectedRows} affected row${result.affectedRows === 1 ? "" : "s"}.`} />;
  }
  if (result.rows.length === 0) return <EmptyResult title="Empty result set" description="The query completed successfully and returned no rows." />;

  return (
    <div className="flex h-full min-h-0 flex-col">
      <ResultToolbar
        rowCount={result.rows.length}
        pageIndex={table.getState().pagination.pageIndex}
        pageCount={table.getPageCount()}
        canPrevious={table.getCanPreviousPage()}
        canNext={table.getCanNextPage()}
        onPrevious={() => table.previousPage()}
        onNext={() => table.nextPage()}
        onCopyTsv={() => void navigator.clipboard.writeText(resultToTsv(result.columns, result.rows))}
        onExportCsv={() => downloadCsv("pagecraft-results.csv", resultToCsv(result.columns, result.rows))}
      />
      <div className="min-h-0 flex-1 overflow-auto">
        <table className="w-max min-w-full border-collapse text-xs">
          <thead className="sticky top-0 z-10 bg-[var(--surface-elevated)]">
            {table.getHeaderGroups().map((group) => (
              <tr key={group.id}>
                <th className="result-header w-12 text-center">#</th>
                {group.headers.map((header) => (
                  <th key={header.id} className="result-header relative" style={{ width: header.getSize() }}>
                    {flexRender(header.column.columnDef.header, header.getContext())}
                    <div
                      className="absolute right-0 top-0 h-full w-1 cursor-col-resize touch-none hover:bg-brand"
                      onMouseDown={header.getResizeHandler()}
                      onTouchStart={header.getResizeHandler()}
                    />
                  </th>
                ))}
              </tr>
            ))}
          </thead>
          <tbody>
            {table.getRowModel().rows.map((row, rowIndex) => (
              <tr key={row.id} className="group hover:bg-brand/5">
                <td className="result-cell text-center text-[var(--muted)]">{table.getState().pagination.pageIndex * 50 + rowIndex + 1}</td>
                {row.getVisibleCells().map((cell) => (
                  <td key={cell.id} className="result-cell" style={{ width: cell.column.getSize(), maxWidth: cell.column.getSize() }}>
                    {flexRender(cell.column.columnDef.cell, cell.getContext())}
                  </td>
                ))}
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
