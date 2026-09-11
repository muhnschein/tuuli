# 0004 — One SQLite file, synchronous access, no worker thread

## Context
sailfish-browser runs its database on a worker thread and stores bookmarks as JSON.

## Decision
One SQLite file under `AppDataLocation` with four tables (see ARCHITECTURE.md). Queries
run on the UI thread. History is capped at 2000 rows and the model shows 500. Bookmarks
live in SQLite too so Phase 2 folders are a column.

## Consequences
Simpler code and tests; every model is fully testable in-process. The cap keeps the
largest query small. Should a query ever show in the smoke test, moving `Storage` behind
a worker changes no model API.
