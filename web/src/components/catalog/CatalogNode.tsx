import { ChevronDown, ChevronRight, Columns3, Copy, Database, Hash, Table2 } from "lucide-react";
import { useState, type PropsWithChildren } from "react";

interface CatalogNodeProps extends PropsWithChildren {
  label: string;
  kind: "database" | "table" | "column" | "index";
  detail?: string;
  defaultOpen?: boolean;
  copyValue?: string;
}

const icons = {
  database: Database,
  table: Table2,
  column: Columns3,
  index: Hash,
};

export function CatalogNode({ label, kind, detail, defaultOpen = false, copyValue, children }: CatalogNodeProps) {
  const [open, setOpen] = useState(defaultOpen);
  const Icon = icons[kind];
  const expandable = Boolean(children);

  return (
    <div className="select-none">
      <div className="group flex min-h-7 items-center gap-1 rounded px-1.5 text-xs hover:bg-[var(--hover)]">
        <button
          className="grid h-5 w-4 place-items-center text-[var(--muted)] disabled:opacity-0"
          onClick={() => setOpen(!open)}
          disabled={!expandable}
          aria-label={`${open ? "Collapse" : "Expand"} ${label}`}
        >
          {open ? <ChevronDown size={13} /> : <ChevronRight size={13} />}
        </button>
        <Icon size={14} className={kind === "table" ? "text-brand" : "text-[var(--muted)]"} />
        <span className="ml-1 truncate text-[var(--text-secondary)]">{label}</span>
        {detail && <span className="ml-auto truncate text-[10px] text-[var(--muted)]">{detail}</span>}
        {copyValue && (
          <button
            className="ml-1 hidden text-[var(--muted)] hover:text-brand group-hover:block"
            aria-label={`Copy ${label}`}
            onClick={() => void navigator.clipboard.writeText(copyValue)}
          ><Copy size={12} /></button>
        )}
      </div>
      {open && children && <div className="ml-4 border-l border-[var(--border)] pl-1">{children}</div>}
    </div>
  );
}
