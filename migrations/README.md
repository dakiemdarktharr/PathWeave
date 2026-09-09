# Database migrations

The schema registry is shared by SQL and native field editors. The released v1
schema is upgraded transactionally to v2 by creating the CV draft table and adding
optional CV-safe descriptions, document content/hash, saved-search scheduling and
job requirement fields. Existing rows and foreign keys are preserved; job identity
keys are scoped to avoid collapsing distinct requisitions. New databases are
created directly at v2. The legacy v1 JSON backup envelope remains importable.

`schema_migrations` records the applied version. Newer database versions are
rejected instead of downgraded. Foreign keys, partial unique indexes and FTS5
triggers remain required. Tests open a legacy database, preserve its records, add
new fields, restart and verify idempotence. Future public changes require another
explicit migration version; never silently mutate a released v2 schema.
