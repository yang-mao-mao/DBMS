import { sql } from "@codemirror/lang-sql";
import CodeMirror, { type ReactCodeMirrorRef } from "@uiw/react-codemirror";
import { FileCode2, Plus, X } from "lucide-react";
import { useRef, useState, type KeyboardEvent } from "react";
import { useWorkspaceStore } from "../../stores/workspaceStore";
import { QueryToolbar } from "./QueryToolbar";

interface SqlEditorProps {
  running: boolean;
  onExecute(sql: string): void;
}

export function SqlEditor({ running, onExecute }: SqlEditorProps) {
  const sqlText = useWorkspaceStore((state) => state.sql);
  const setSql = useWorkspaceStore((state) => state.setSql);
  const theme = useWorkspaceStore((state) => state.theme);
  const editorRef = useRef<ReactCodeMirrorRef>(null);
  const [fontSize, setFontSize] = useState(14);

  const selectedOrAll = (): string => {
    const view = editorRef.current?.view;
    if (!view) return sqlText;
    const selection = view.state.selection.main;
    return selection.empty ? sqlText : view.state.sliceDoc(selection.from, selection.to);
  };

  const execute = (): void => {
    const statement = selectedOrAll().trim();
    if (statement) onExecute(statement);
  };

  const handleShortcut = (event: KeyboardEvent<HTMLDivElement>): void => {
    if ((event.ctrlKey && event.key === "Enter") || event.key === "F5") {
      event.preventDefault();
      execute();
    } else if (event.ctrlKey && event.key.toLowerCase() === "l") {
      event.preventDefault();
      setSql("");
    }
  };

  return (
    <section className="flex h-full min-h-0 flex-col bg-[var(--background)]" aria-label="SQL editor">
      <div className="flex h-9 shrink-0 items-end border-b border-[var(--border)] bg-[var(--surface)] px-2">
        <div className="flex h-8 min-w-36 items-center gap-2 rounded-t-md border-x border-t border-[var(--border)] bg-[var(--background)] px-3 text-xs">
          <FileCode2 size={13} className="text-brand" /><span>Query 1</span><X size={12} className="ml-auto text-[var(--muted)]" />
        </div>
        <button className="ml-1 grid h-7 w-7 place-items-center text-[var(--muted)] hover:text-brand" aria-label="New query tab"><Plus size={14} /></button>
      </div>
      <QueryToolbar
        running={running}
        fontSize={fontSize}
        onRun={execute}
        onClear={() => setSql("")}
        onFontSize={setFontSize}
        onExample={setSql}
      />
      <div className="min-h-0 flex-1 overflow-hidden" style={{ fontSize }} onKeyDownCapture={handleShortcut}>
        <CodeMirror
          ref={editorRef}
          value={sqlText}
          height="100%"
          minHeight="100%"
          theme={theme}
          extensions={[sql()]}
          onChange={setSql}
          basicSetup={{
            lineNumbers: true,
            highlightActiveLine: true,
            highlightActiveLineGutter: true,
            bracketMatching: true,
            closeBrackets: true,
            foldGutter: true,
          }}
          className="h-full [&_.cm-editor]:h-full [&_.cm-scroller]:font-mono"
          aria-label="SQL query editor"
        />
      </div>
    </section>
  );
}
