# Windows code signing

Status: **deferred**. Releases ship an unsigned exe, with the `.zip` as the fallback for anyone whose AV objects to the packed single exe.

## Where this stands

- SignPath Foundation was the chosen path for released binaries. The application was refused, so nothing signs the release today.

- The release-only GitHub Actions workflow exists *because* SignPath would only sign artifacts from a verifiable CI build. With that gone, nothing forces the release build into hosted CI, and a local `cicd-win.ps1` cut is viable again. That matters because most of the remaining options sign locally rather than in CI.

- `.github/workflows/release-win.yml` is kept as it is. It still builds and packs, and hands the exe to the local cut, which makes the release; the SignPath submission step stays dormant behind its `SIGNPATH_API_TOKEN` gate, so there is nothing to unpick if this is picked back up. The repo has no secrets or variables set, and the step is confirmed skipped on the last tag build.

## Options, if this is revisited

Backdrop for any comparison: since the 2026 changes, no certificate tier buys immediate SmartScreen clearance, EV included. Reputation accrues with download volume regardless of what was paid, which argues for the cheapest workable option rather than the most trusted-sounding one.

- **Azure Artifact Signing** (renamed from Trusted Signing): about $10/month on the Basic tier, no hardware, short-lived certificates issued per-sign from a Microsoft CA, and the publisher shown is the validated identity of whoever signs. Best CI fit by far - swap the SignPath step for the Azure signing action and the rest of the workflow stands. The gate is eligibility: individual (non-organization) sign-up is limited to the USA and Canada, and organization validation wants three years of verifiable legal existence.

- **Certum Open Source Code Signing**: roughly EUR 69 up front for the certificate, smartcard and reader, then about EUR 29 a year. Individuals only, and revoked if used for commercially distributed software, which is fine here. The certificate's Organization field reads "Open Source Developer" rather than the project - the same drawback that SignPath had. The hardware card makes hosted CI awkward, but the SimplySign cloud variant presents as a virtual card and would drop straight into the `NEMO_SIGN_THUMBPRINT` path below.

- **Commercial OV/EV cloud** (SSL.com eSigner, DigiCert and the like): a few hundred a year. Only worth it if the publisher string has to read as the project itself.

- **Reapplying to SignPath**: not ruled out. If it is tried, one thing to check first is whether the packed Enigma Virtual Box exe is a problem for them, since it is opaque to build verification - the same property that drives the AV false-positive item on the backlog.

## Local signtool (a certificate you hold yourself)

Independent of the above and already working. `cicd-win.ps1` stage 5 signs the packed exe via `signtool`, driven entirely by env vars, so nothing secret lives in the repo:

- `NEMO_SIGN_THUMBPRINT` - SHA-1 thumbprint of an installed certificate (store or hardware token). Preferred.

- `NEMO_SIGN_PFX` (+ `NEMO_SIGN_PFX_PASSWORD`) - a `.pfx` on disk (testing / self-signed).

- `NEMO_SIGN_TS_URL` - RFC-3161 timestamp URL (defaults to a public TSA).

- `NEMO_SIGNTOOL` - explicit `signtool.exe` path (else PATH, then the newest Windows SDK build).

Unconfigured, signing is a no-op with a note, so the unsigned dev flow is never blocked. `-NoSign` skips it even when configured. This path signs with the identity of whoever holds the certificate as the publisher and needs no CI.
