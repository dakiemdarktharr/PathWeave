# PathWeave 1.1 — CV and discovery

## User workflow

1. Complete profile contact information, summary, education and certifications.
2. Add current/past roles and their CV-safe descriptions; add projects/evidence.
3. Open a job or application and select **Tạo CV theo JD**; alternatively create
   a CV in Applications → CV theo công việc and paste a JD.
4. Choose shareable records. Only current/past role headers are selected initially;
   choose projects, achievements or career evidence explicitly. Private notes and
   demo records are excluded. Evidence and its original achievement are deduplicated.
5. Generate, edit plain text, add an optional personal image, review, and export.
6. Closing a nonempty edited draft saves a new version automatically; unchanged
   drafts do not create duplicates. Save versions or attach a reviewed PDF to an application. Previous documents
   remain available. Attaching never submits an application or changes its status.

PDF and HTML contain only escaped text and an optional embedded image. Images are
resized/re-encoded as PNG without original metadata. TXT excludes the image.
JD requirements missing from the candidate's data are shown separately, never
inserted as accomplishments. Facts/certificates remain user-entered, not verified.

## Discovery behavior

- Query text is searched by words with Vietnamese/English normalization.
- Empty online keywords use up to five desired/alternative titles; Adzuna accepts
  location/type and a configurable 1–3 pages. Public boards/feeds are fetched once.
- The Recommendations tab has a configurable minimum score (default 40) and count.
- New Today means the actual posted date in the local timezone, not fetch date.
- Skills include real skill records, achievements, career evidence and work logs.
- Save / create application / dismiss supply transparent type/mode votes. The
  adjustment is bounded to ±4 points; disable it in Settings to use base scores only.
- Excluded companies remain hidden from discovery after later imports. Saved jobs
  remain accessible separately. Unknown remote status never becomes remote.
- Enable online search AND the scheduling option before automatic work. Each saved
  search additionally has enabled/interval/next_run. Missed runs resume when the app
  runs again. A profile's daily/weekly frequency uses a persisted next-run timestamp.
- Scheduled runs do not navigate away from the current page or open approval dialogs.
- Network and source errors remain visible. Public APIs can impose quotas that
  prevent immediate execution; no anti-bot or login restrictions are bypassed.

## Privacy and data

Managed documents and images are embedded in JSON backups. Default `.pwbackup`
files use Windows CNG AES-256-GCM, a random 128-bit salt and 96-bit nonce, a 128-bit
authentication tag, and PBKDF2-HMAC-SHA256 with 600,000 iterations. The password is
not stored. Header version/salt/nonce are authenticated. Wrong passwords, truncated
files and altered ciphertext fail before database replacement. SQL import remains
transactional and validates foreign keys/document hashes. Backup size is bounded.

EFS is a separate opt-in Windows feature, not SQLCipher. It encrypts the data
folder/main file at rest on supported installations, with explicit failure reporting.
Keep a portable backup and export the Windows EFS recovery certificate separately.

Technical references:
- [Windows CNG PBKDF2](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptderivekeypbkdf2)
- [Authenticated cipher mode](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/ns-bcrypt-bcrypt_authenticated_cipher_mode_info)
- [Windows EFS](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-encryptfilew)
- [Robots Exclusion Protocol](https://www.rfc-editor.org/rfc/rfc9309.html)

## Remaining dependencies

No LinkedIn/VietnamWorks integration. Source credentials, code-signing identity,
calendar provider OAuth setup and an update hosting endpoint are not fabricated.
This release does not claim these external capabilities are verified.
