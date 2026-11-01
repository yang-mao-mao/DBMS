import { useQuery } from "@tanstack/react-query";
import { useDatabaseService } from "../app/serviceContext";

export const useCatalog = () => {
  const { service, mode } = useDatabaseService();
  return useQuery({
    queryKey: ["catalog", mode],
    queryFn: () => service.getCatalog(),
  });
};
