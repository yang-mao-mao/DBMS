import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { render, type RenderResult } from "@testing-library/react";
import type { ReactElement } from "react";
import { ServiceContext } from "../app/serviceContext";
import type { DatabaseService } from "../services/DatabaseService";
import type { ExecutionMode } from "../types/database";

export const renderWithService = (
  element: ReactElement,
  service: DatabaseService,
  mode: ExecutionMode = "real",
): RenderResult => {
  const client = new QueryClient({ defaultOptions: { queries: { retry: false }, mutations: { retry: false } } });
  return render(
    <QueryClientProvider client={client}>
      <ServiceContext.Provider value={{ service, mode, useDemoMode: vi.fn(), retryRealMode: vi.fn() }}>
        {element}
      </ServiceContext.Provider>
    </QueryClientProvider>,
  );
};
