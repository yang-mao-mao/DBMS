import { X } from "lucide-react";
import { PageCraftLogo } from "./PageCraftLogo";

interface AboutDialogProps {
  open: boolean;
  onClose(): void;
}

export function AboutDialog({ open, onClose }: AboutDialogProps) {
  if (!open) return null;
  return (
    <div className="fixed inset-0 z-50 grid place-items-center bg-black/55 p-4" role="presentation" onMouseDown={onClose}>
      <section
        role="dialog"
        aria-modal="true"
        aria-labelledby="about-title"
        className="w-full max-w-lg rounded-xl border border-[var(--border)] bg-[var(--panel)] p-6 shadow-panel"
        onMouseDown={(event) => event.stopPropagation()}
      >
        <div className="flex items-start justify-between">
          <PageCraftLogo size={40} />
          <button className="icon-button" onClick={onClose} aria-label="Close about dialog"><X size={17} /></button>
        </div>
        <h2 id="about-title" className="mt-6 text-xl font-semibold">PageCraft DB</h2>
        <p className="mt-1 text-sm text-[var(--muted)]">A Lightweight Relational Database Built Page by Page</p>
        <p className="mt-5 text-sm leading-6 text-[var(--text-secondary)]">
          以页面为基石构建的轻量级关系型数据库。PageCraft Studio 提供查询、结果浏览、Catalog 探索与执行计划可视化。
        </p>
        <div className="mt-5 rounded-lg border border-amber-400/25 bg-amber-400/10 p-3 text-xs leading-5 text-amber-300 dark:text-amber-200">
          Demo mode uses local Mock data for interface preview only. It never represents execution by the real PageCraft DB kernel.
        </div>
        <div className="mt-6 text-xs text-[var(--muted)]">C++20 kernel · React Studio · Course project edition</div>
      </section>
    </div>
  );
}
