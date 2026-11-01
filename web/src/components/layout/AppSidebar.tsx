import { CatalogTree } from "../catalog/CatalogTree";

export function AppSidebar() {
  return (
    <aside className="h-full min-h-0 overflow-hidden border-r border-[var(--border)] bg-[var(--surface)]">
      <CatalogTree />
    </aside>
  );
}
