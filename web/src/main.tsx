import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import App from "./app/App";
import { AppProviders } from "./app/providers";
import "./styles/index.css";

const rootElement = document.getElementById("root");
if (!rootElement) throw new Error("PageCraft Studio root element is missing");

createRoot(rootElement).render(
  <StrictMode>
    <AppProviders><App /></AppProviders>
  </StrictMode>,
);
