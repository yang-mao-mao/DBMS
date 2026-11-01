import { ChevronLeft, ChevronRight, Copy, Download, Rows3 } from "lucide-react";

interface ResultToolbarProps {
  rowCount: number;
  pageIndex: number;
  pageCount: number;
  canPrevious: boolean;
  canNext: boolean;
  onPrevious(): void;
  onNext(): void;
  onCopyTsv(): void;
  onExportCsv(): void;
}

export function ResultToolbar(props: ResultToolbarProps) {
  return (
    <div className="flex h-9 shrink-0 items-center border-b border-[var(--border)] px-2 text-xs">
      <Rows3 size={13} className="mr-1.5 text-brand" />
      <span className="text-[var(--muted)]">{props.rowCount} {props.rowCount === 1 ? "row" : "rows"}</span>
      <div className="ml-auto flex items-center gap-1">
        <button className="toolbar-button compact" onClick={props.onCopyTsv}><Copy size={12} /> Copy TSV</button>
        <button className="toolbar-button compact" onClick={props.onExportCsv}><Download size={12} /> CSV</button>
        <div className="mx-1 h-4 w-px bg-[var(--border)]" />
        <button className="icon-button h-6 w-6" disabled={!props.canPrevious} onClick={props.onPrevious} aria-label="Previous page"><ChevronLeft size={13} /></button>
        <span className="min-w-14 text-center text-[10px] text-[var(--muted)]">{props.pageIndex + 1} / {Math.max(props.pageCount, 1)}</span>
        <button className="icon-button h-6 w-6" disabled={!props.canNext} onClick={props.onNext} aria-label="Next page"><ChevronRight size={13} /></button>
      </div>
    </div>
  );
}
