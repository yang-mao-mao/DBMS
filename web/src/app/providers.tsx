import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { useMemo, useState, type PropsWithChildren } from "react";
import { HttpDatabaseService } from "../services/HttpDatabaseService";
import { MockDatabaseService } from "../services/MockDatabaseService";
import { useWorkspaceStore } from "../stores/workspaceStore";
import { ServiceContext } from "./serviceContext";

const environment = (import.meta as unknown as { env: Record<string, unknown> }).env;
const configuredApiUrl = environment["VITE_PAGECRAFT_API_URL"];
const apiUrl = typeof configuredApiUrl === "string" ? configuredApiUrl : "http://127.0.0.1:8765";

export function AppProviders({ children }: PropsWithChildren) {
  const [queryClient] = useState(() => new QueryClient({
    defaultOptions: { queries: { retry: false, staleTime: 15_000 } },
  }));
  const mode = useWorkspaceStore((state) => state.mode);
  const setMode = useWorkspaceStore((state) => state.setMode);
  const http = useMemo(() => new HttpDatabaseService(apiUrl), []);
  const mock = useMemo(() => new MockDatabaseService(), []);
  const service = mode === "real" ? http : mock;

  const value = useMemo(
    () => ({
      service,
      mode,
      useDemoMode: () => {
        setMode("demo");
        void queryClient.invalidateQueries();
      },
      retryRealMode: () => {
        setMode("real");
        void queryClient.invalidateQueries();
      },
    }),
    [mode, queryClient, service, setMode],
  );

  return (
    <QueryClientProvider client={queryClient}>
      <ServiceContext.Provider value={value}>{children}</ServiceContext.Provider>
    </QueryClientProvider>
  );
}
