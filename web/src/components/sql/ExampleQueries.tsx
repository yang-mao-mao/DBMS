import { BookOpen, ChevronDown } from "lucide-react";
import { useState } from "react";

export const exampleQueries = [
  { label: "Create students", sql: "CREATE TABLE students (\n  id INT,\n  name VARCHAR(32),\n  age INT\n);" },
  { label: "Insert Alice", sql: "INSERT INTO students VALUES (1, 'Alice', 20);" },
  { label: "Browse students", sql: "SELECT * FROM students;" },
  { label: "Filter by age", sql: "SELECT name\nFROM students\nWHERE age > 20;" },
  { label: "Explain scan", sql: "EXPLAIN SELECT name\nFROM students\nWHERE age > 20;" },
  { label: "Try index", sql: "CREATE INDEX idx_id ON students(id);" },
];

interface ExampleQueriesProps {
  onSelect(sql: string): void;
}

export function ExampleQueries({ onSelect }: ExampleQueriesProps) {
  const [open, setOpen] = useState(false);
  return (
    <div className="relative">
      <button className="toolbar-button" onClick={() => setOpen(!open)}><BookOpen size={14} /> Examples <ChevronDown size={12} /></button>
      {open && (
        <div className="absolute right-0 top-9 z-30 w-56 rounded-lg border border-[var(--border)] bg-[var(--panel)] p-1.5 shadow-panel">
          {exampleQueries.map((example) => (
            <button
              key={example.label}
              className="block w-full rounded px-3 py-2 text-left text-xs text-[var(--text-secondary)] hover:bg-[var(--hover)] hover:text-[var(--text)]"
              onClick={() => { onSelect(example.sql); setOpen(false); }}
            >{example.label}</button>
          ))}
        </div>
      )}
    </div>
  );
}
