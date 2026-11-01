import { AlertTriangle, LoaderCircle, RefreshCw, Search } from "lucide-react";
import { useState } from "react";
import { useCatalog } from "../../hooks/useCatalog";
import { useWorkspaceStore } from "../../stores/workspaceStore";
import { CatalogNode } from "./CatalogNode";

export function CatalogTree() {
  const [search, setSearch] = useState("");
  const setSql = useWorkspaceStore((state) => state.setSql);
  const catalog = useCatalog();

  return (
    <div className="flex h-full min-h-0 flex-col">
      <div className="flex h-10 shrink-0 items-center border-b border-[var(--border)] px-3">
        <span className="panel-title">CATALOG</span>
        <button className="icon-button ml-auto h-7 w-7" onClick={() => void catalog.refetch()} aria-label="Refresh catalog">
          <RefreshCw size={13} className={catalog.isFetching ? "animate-spin" : ""} />
        </button>
      </div>
      <div className="relative m-2">
        <Search size={13} className="absolute left-2 top-2 text-[var(--muted)]" />
        <input
          className="h-7 w-full rounded border border-[var(--border)] bg-[var(--background)] pl-7 pr-2 text-xs outline-none focus:border-brand"
          value={search}
          onChange={(event) => setSearch(event.target.value)}
          placeholder="Filter objects"
          aria-label="Filter catalog"
        />
      </div>
      <div className="min-h-0 flex-1 overflow-auto px-2 pb-3">
        {catalog.isLoading && <div className="empty-inline"><LoaderCircle size={15} className="animate-spin" /> Loading catalog…</div>}
        {catalog.isError && (
          <div className="m-1 rounded-lg border border-amber-400/20 bg-amber-400/5 p-3 text-xs text-[var(--muted)]">
            <AlertTriangle size={15} className="mb-2 text-amber-400" />
            Catalog list API is not available.
          </div>
        )}
        {catalog.data && (
          <CatalogNode label="Tables" kind="database" detail={String(catalog.data.tables.length)} defaultOpen>
            {catalog.data.tables
              .filter((table) => table.name.toLowerCase().includes(search.toLowerCase()))
              .map((table) => (
                <CatalogNode key={table.id} label={table.name} kind="table" detail={`#${table.id}`} defaultOpen copyValue={table.name}>
                  {table.columns.map((column) => (
                    <CatalogNode
                      key={column.name}
                      label={column.name}
                      kind="column"
                      detail={`${column.type}${column.maxLength ? `(${column.maxLength})` : ""}${column.nullable ? " · NULL" : ""}`}
                      copyValue={column.name}
                    />
                  ))}
                  <button
                    className="ml-5 mt-1 rounded px-2 py-1 text-[10px] text-brand hover:bg-brand/10"
                    onClick={() => setSql(`SELECT * FROM ${table.name};`)}
                  >Generate SELECT *</button>
                  {table.indexes.map((index) => (
                    <CatalogNode key={index.name} label={index.name} kind="index" detail={index.state} copyValue={index.name} />
                  ))}
                </CatalogNode>
              ))}
          </CatalogNode>
        )}
      </div>
    </div>
  );
}
