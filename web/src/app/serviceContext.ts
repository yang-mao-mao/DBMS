import { createContext, useContext } from "react";
import type { DatabaseService } from "../services/DatabaseService";
import type { ExecutionMode } from "../types/database";

export interface ServiceContextValue {
  service: DatabaseService;
  mode: ExecutionMode;
  useDemoMode(): void;
  retryRealMode(): void;
}

export const ServiceContext = createContext<ServiceContextValue | null>(null);

export const useDatabaseService = (): ServiceContextValue => {
  const value = useContext(ServiceContext);
  if (!value) throw new Error("Database service provider is missing");
  return value;
};
