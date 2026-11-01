import { Rows3 } from "lucide-react";

interface EmptyResultProps {
  title?: string;
  description?: string;
}

export function EmptyResult({ title = "No result set", description = "Run a query to see rows here." }: EmptyResultProps) {
  return (
    <div className="grid h-full min-h-40 place-items-center p-6 text-center">
      <div>
        <div className="mx-auto grid h-11 w-11 place-items-center rounded-xl border border-[var(--border)] bg-[var(--surface)] text-[var(--muted)]"><Rows3 size={20} /></div>
        <div className="mt-3 text-sm font-medium">{title}</div>
        <div className="mt-1 text-xs text-[var(--muted)]">{description}</div>
      </div>
    </div>
  );
}
