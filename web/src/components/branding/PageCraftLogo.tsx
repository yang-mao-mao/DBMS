interface PageCraftLogoProps {
  size?: number;
  showWordmark?: boolean;
}

export function PageCraftLogo({ size = 30, showWordmark = true }: PageCraftLogoProps) {
  return (
    <div className="flex items-center gap-2.5" aria-label="PageCraft Studio">
      <svg width={size} height={size} viewBox="0 0 64 64" role="img" aria-label="PageCraft page stack logo">
        <rect x="9" y="9" width="35" height="39" rx="5" className="fill-brand/15 stroke-brand/45" strokeWidth="3" />
        <path d="M17 16h28l8 8v31H17z" className="fill-[var(--panel)] stroke-brand" strokeWidth="3" strokeLinejoin="round" />
        <path d="M45 16v10h8" fill="none" className="stroke-brand" strokeWidth="3" strokeLinejoin="round" />
        <path d="M25 33h20M25 40h20M25 47h13" className="stroke-[var(--text)]" strokeWidth="3" strokeLinecap="round" />
      </svg>
      {showWordmark && (
        <div className="leading-none">
          <div className="text-[15px] font-semibold tracking-tight text-[var(--text)]">PageCraft Studio</div>
          <div className="mt-1 text-[9px] font-medium uppercase tracking-[0.18em] text-[var(--muted)]">Page by page</div>
        </div>
      )}
    </div>
  );
}
