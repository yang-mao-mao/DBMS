import { Eraser, LoaderCircle, Play, Plus, TextCursorInput } from "lucide-react";
import { ExampleQueries } from "./ExampleQueries";

interface QueryToolbarProps {
  running: boolean;
  fontSize: number;
  onRun(): void;
  onClear(): void;
  onFontSize(size: number): void;
  onExample(sql: string): void;
}

export function QueryToolbar({ running, fontSize, onRun, onClear, onFontSize, onExample }: QueryToolbarProps) {
  return (
    <div className="flex h-10 shrink-0 items-center gap-1 border-b border-[var(--border)] bg-[var(--surface)] px-2">
      <button className="run-button" onClick={onRun} disabled={running}>
        {running ? <LoaderCircle size={14} className="animate-spin" /> : <Play size={14} fill="currentColor" />}
        {running ? "Running" : "Run"}
      </button>
      <span className="ml-1 hidden text-[10px] text-[var(--muted)] sm:inline">Ctrl + Enter</span>
      <div className="mx-2 h-5 w-px bg-[var(--border)]" />
      <button className="icon-button h-7 w-7" onClick={onClear} aria-label="Clear SQL"><Eraser size={14} /></button>
      <div className="ml-auto flex items-center gap-1">
        <TextCursorInput size={13} className="text-[var(--muted)]" />
        <button className="font-button" onClick={() => onFontSize(Math.max(11, fontSize - 1))}>−</button>
        <span className="w-5 text-center text-[10px] text-[var(--muted)]">{fontSize}</span>
        <button className="font-button" onClick={() => onFontSize(Math.min(20, fontSize + 1))}><Plus size={11} /></button>
      </div>
      <ExampleQueries onSelect={onExample} />
    </div>
  );
}
