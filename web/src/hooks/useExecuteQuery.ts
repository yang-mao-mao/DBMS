import { useMutation, useQueryClient } from "@tanstack/react-query";
import { useDatabaseService } from "../app/serviceContext";
import { useWorkspaceStore } from "../stores/workspaceStore";

const newId = (): string => `${Date.now()}-${Math.random().toString(36).slice(2)}`;

export const useExecuteQuery = () => {
  const { service, mode } = useDatabaseService();
  const setLastResult = useWorkspaceStore((state) => state.setLastResult);
  const setResultTab = useWorkspaceStore((state) => state.setResultTab);
  const addHistory = useWorkspaceStore((state) => state.addHistory);
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: (sql: string) => service.execute(sql),
    onSuccess: (result, sql) => {
      setLastResult(result);
      setResultTab(result.formattedPlan || result.plan ? "explain" : result.success ? "results" : "messages");
      addHistory({
        id: newId(),
        sql,
        executedAt: new Date().toISOString(),
        success: result.success,
        rowCount: result.rows.length,
        affectedRows: result.affectedRows,
        elapsedMs: result.elapsedMs,
        ...(result.error ? { errorSummary: result.error.message } : {}),
        mode,
      });
      const changesCatalog = /^\s*CREATE\s+(TABLE|INDEX)\b/i.test(sql);
      if (result.success && (result.affectedRows > 0 || changesCatalog)) {
        void queryClient.invalidateQueries({ queryKey: ["catalog"] });
      }
    },
    onError: (error: Error, sql) => {
      const result = {
        success: false,
        columns: [],
        rows: [],
        affectedRows: 0,
        elapsedMs: 0,
        message: error.message,
        error: { code: "CLIENT_ERROR", message: error.message },
      };
      setLastResult(result);
      setResultTab("messages");
      addHistory({
        id: newId(),
        sql,
        executedAt: new Date().toISOString(),
        success: false,
        rowCount: 0,
        affectedRows: 0,
        elapsedMs: 0,
        errorSummary: error.message,
        mode,
      });
    },
  });
};
