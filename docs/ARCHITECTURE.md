# Architecture and data contracts

PathWeave is a single-process native Windows application. The Widgets UI calls
CareerService, which owns domain transitions and evidence conversion. Database
owns a named QSQLITE connection and transactional versioned migrations. Reports
query the same local data; they never contact a service.

`Schema.cpp` is the typed entity registry for columns, native field widgets,
required fields, enums and foreign-key pickers. Internal SQL identifiers are
allowlisted through this registry. Search expressions are quoted as FTS phrases.

Job listings and applications have independent lifecycles. Saving a listing
creates `saved_jobs`; preparing an application is a separate explicit action.
Status changes append `application_activities` in the same transaction.
Archived applications can reopen to saved; rejected/withdrawn records can reopen
to preparing or archive. Other recognized active statuses allow correction of
out-of-order historical data. Original timestamps remain in the activity history.

Evidence conversion copies entered title, body, measurable result, skills,
project and date. The user edits the resulting summary. No numerical improvement,
achievement or AI rewrite is generated. Evidence-to-application links use the
`application_evidence` join table. Skills support both text tags and explicit
project/achievement join tables. Achievement skill edits maintain relational links;
skill renames update linked text labels transactionally. Managed document copies
are embedded as base64 with SHA-256 and included in backups. They are never uploaded.

All domain tables include created_at, updated_at, deleted_at and is_demo. The
provider's update timestamp is `updated_at_source`, distinct from the local
row timestamp. Partial unique indexes protect live job URL, source/external ID,
and scoped company-title identities. Different requisition URLs are not merged. Soft deletion removes records from FTS.

NetworkService owns one QNetworkAccessManager. It performs only HTTPS GETs and
disables automatic redirects and cookies; a bounded manual chain permits same-origin
HTTPS redirects with one absolute deadline and a robots guard for arbitrary sources. Errors do not include request URLs
or credentials. Successful raw responses are cached with expiry under a SHA-256
request key; settings can clear them. Credential values are kept only in DPAPI
files and transient request objects. Arbitrary JSON/RSS/page connectors first
check robots.txt using matching user-agent groups and longest-path Allow/Disallow
rules, including wildcard/end anchors and percent-encoding normalization.
Known APIs use documented public endpoints. Authentication and anti-bot failures
are surfaced without retries or circumvention.

Matching uses normalized Vietnamese/English titles, a curated synonym dictionary,
skills from profile and real work records, and explicit
profile choices. Missing salary/currency/period is neutral. No FX conversion or
geographic eligibility inference is attempted. Freshness is shown separately
and defaults to zero weight. Weights are normalized by their positive total.
Excluded keyword hits subtract 30 points; excluded companies score zero.
Save/dismiss/application actions persist locally; hide-company explicitly adds
an excluded company. An optional ±4 point adjustment reflects explicit save,
application creation and dismiss votes for employment/workplace categories. There
is no behavioral tracking, remote model or automatic inference of achievements.

JSON restore is transactional replacement. It validates format/version/table and
column names, applies SQLite constraints, checks foreign keys, and forces online
search off and clears response cache. Exports contain CV snapshots, images and
managed document bytes, not credentials. Portable password backups wrap the JSON
with Windows CNG AES-256-GCM/PBKDF2; plaintext JSON remains an explicit option.
CSV escapes quotes and prefixes formula-leading cells.

Full JSON backups include soft-deleted rows to preserve references from live children.
They are restored as deleted, and remain excluded from normal views and FTS.


ResumeBuilder selects and orders only user-entered facts against a job snapshot.
ResumeDialog provides optional photo import, editable plain text, HTML/PDF preview,
review gating, immutable saved versions and attachment without submission. HTML
escapes all text and embeds only validated/re-encoded PNG images. Private role notes
are separate from CV-safe descriptions. The composer never treats JD requirements
as evidence that the user has a skill/certificate/achievement.

DiscoveryPolicy owns pure query/filter/schedule rules. MainWindow queues source
requests with rate-aware delays. A separately enabled QTimer scheduler checks
persisted due times; optional system tray mode keeps the process running. It does
not run when the process/PC is off. Calendar notifications use persisted event
keys; ICS export converts timed events to UTC and keeps date-only deadlines.

Reports exclude demo data, distinguish current-state counts from application
cohorts, and count only observed status events. Durations exclude unfinished
intervals rather than fabricating transitions. EFS encryption is opt-in, native to
Windows and requires a supported filesystem/policy plus user-managed recovery.
