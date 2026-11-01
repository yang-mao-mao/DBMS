# PageCraft Studio

PageCraft Studio is the Web management interface for **PageCraft DB** — *A Lightweight Relational Database Built Page by Page*.

> 以页面为基石构建的轻量级关系型数据库

The Studio is a React application in the repository's `web` directory. It never reads database files directly and does not contain a second SQL parser. All real execution is delegated to the included local C++20 HTTP bridge, which calls the database kernel's existing `Database::execute()` entry point.

## Features

- Professional dark and light database workbench
- CodeMirror SQL editor with selection execution, `Ctrl+Enter`, `F5`, and `Ctrl+L`
- Dynamic TanStack result table, pagination, column resizing, copy, TSV and CSV export
- Catalog tree with tables, columns, `TableId`, VARCHAR lengths and index state
- Text and React Flow EXPLAIN views
- Persistent local query history
- Explicit REAL MODE / DEMO MODE boundary
- Runtime validation of every HTTP response
- Responsive, collapsible and resizable panels

## Real and demo modes

Studio always starts by checking the real API. If the API is unavailable, the user must explicitly choose **Enter demo mode**. It never falls back silently.

- **REAL MODE** calls the configured PageCraft API.
- **DEMO MODE** uses local Mock data only for UI preview. It does not claim to execute the C++ kernel.

Real Mode uses the C++ kernel's persistent B+ tree for single-column `INT64` equality indexes. `CREATE INDEX`, indexed `EXPLAIN`, indexed lookup and index maintenance during `INSERT`/`DELETE` are executed by the real kernel. Demo Mode remains an isolated UI preview and does not pretend to provide those physical capabilities.

## Start Real Mode on Windows

Requirements:

- Node.js 20+ and npm
- CMake 3.29+
- A C++20 compiler (Visual Studio 2022 with **Desktop development with C++** is recommended)
- Clone this repository; the Web application and C++ kernel are already colocated

Open two PowerShell windows. In the first one, build and start the C++ bridge:

```powershell
cd D:\SJTU\junior\NIS3351\DBMS\web
npm run bridge:start
```

The default persistent database directory is `web\my_database`. To use another directory or port:

```powershell
.\scripts\start-real-mode.ps1 -DatabaseDirectory D:\data\pagecraft -Port 8765
```

Keep that terminal open. In the second PowerShell window, start Studio:

```powershell
cd D:\SJTU\junior\NIS3351\DBMS\web
npm install
npm run dev
```

Open `http://127.0.0.1:5173`. Studio starts in **REAL MODE** and checks the bridge automatically. If it was previously switched to demo mode, click the mode badge/menu and choose **Retry real mode**.

You can verify the bridge before opening Studio:

```powershell
Invoke-RestMethod http://127.0.0.1:8765/api/v1/health
```

## API contract

Local development uses Vite's same-origin `/api` proxy. Keep the URL empty in `.env.local` so the browser only connects to port 5173; Vite forwards API requests to the C++ bridge on port 8765:

```env
VITE_PAGECRAFT_API_URL=.
```

Expected endpoints:

```text
GET  /api/v1/health
GET  /api/v1/catalog
POST /api/v1/query
```

Query body:

```json
{ "sql": "SELECT * FROM students;" }
```

The TypeScript DTOs and response validation live in `src/types/database.ts` and `src/services/validation.ts`.

## Development

Requirements: Node.js 20+ and npm.

```bash
npm install
npm run dev
```

Quality checks:

```bash
npm run lint
npm run typecheck
npm run test
npm run build
```

## Security boundaries

- SQL and errors are rendered as plain text.
- No `dangerouslySetInnerHTML` is used.
- CSV values escape quotes, commas and line breaks.
- No database file is parsed in the browser.
- No data is sent to an internet service.
- No SQLite, DuckDB or MySQL dependency is present.

## Current kernel limitations

The current C++ branch supports `CREATE TABLE`, `INSERT`, `SELECT`, a single `WHERE` predicate, `DELETE`, `CREATE INDEX`, `EXPLAIN`, persistent Catalog/table storage, and persistent single-column `INT64` B+ tree indexes. The included bridge exposes the same `Database::execute()` path to the browser. The kernel still does not support JOIN, subqueries, aggregation, transactions, concurrent execution, UPDATE, VARCHAR indexes, composite indexes, or range index scans.
