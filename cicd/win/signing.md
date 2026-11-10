# Windows code signing

Status: **deferred**. Releases have an unsigned exe, with the `.zip` as the fallback for anyone whose AV objects to the packed single exe.

## Where this stands

- SignPath Foundation was the chosen path for released binaries. The application was refused, so nothing signs the release today.

- The release-only GitHub Actions workflow exists because SignPath would only sign artifacts from a verifiable CI build. With SignPath gone, nothing forces the release build into hosted CI, and a local `cicd-win.ps1` cut works again. Most of the remaining options sign locally rather than in CI anyway.

- `.github/workflows/release-win.yml` is kept as it is. It still builds and packs, and hands the exe to the local cut, which makes the release. The SignPath submission step is off behind its `SIGNPATH_API_TOKEN` gate, so there's nothing to undo if this is picked back up. The repo has no secrets or variables set, and the last tag build skipped the step.

## Options, if this is revisited

Since the 2026 changes, no certificate tier buys immediate SmartScreen clearance, EV included. Reputation builds with download volume whatever was paid, which argues for the cheapest option that works rather than the one that sounds most trusted.

- **Azure Artifact Signing** costs about $10/month on the Basic tier, and was called Trusted Signing before. It needs no hardware, issues short-lived certificates per signing from a Microsoft CA, and shows the validated identity of whoever signs as the publisher. It's the best CI fit by far, since the Azure signing action can replace the SignPath step and the rest of the workflow stays as it is. Eligibility is the catch. Individual sign-up is limited to the USA and Canada, and organization validation wants 3 years of verifiable legal existence.

- **Certum Open Source Code Signing**: Roughly EUR 69 up front for the certificate, smartcard, and reader, then about EUR 29 a year. It's for individuals only, and it's revoked if used for commercially distributed software, which doesn't apply here. The certificate's Organization field reads "Open Source Developer" rather than the project name, which was SignPath's drawback too. The hardware card makes hosted CI awkward, but the SimplySign cloud variant shows up as a virtual card and would work through the `NEMO_SIGN_THUMBPRINT` path below.

- A commercial OV/EV cloud certificate, such as SSL.com eSigner or DigiCert, costs a few hundred a year. It's only worth it if the publisher string has to read as the project itself.

- Reapplying to SignPath isn't ruled out. If it's tried, first check whether the packed Enigma Virtual Box exe is a problem for them, since it's opaque to build verification. That's the same property behind the AV false-positive item on the backlog.

## Local signtool with your own certificate

This works today and doesn't depend on any of the above. `cicd-win.ps1` stage 5 signs the packed exe with `signtool`, set up entirely by env vars, so no secrets are kept in the repo:

- `NEMO_SIGN_THUMBPRINT`: the SHA-1 thumbprint of an installed certificate, in the store or on a hardware token. This one is preferred.

- `NEMO_SIGN_PFX`, with `NEMO_SIGN_PFX_PASSWORD`: a `.pfx` on disk, for testing or a self-signed certificate.

- `NEMO_SIGN_TS_URL`: the RFC-3161 timestamp URL. It defaults to a public TSA.

- `NEMO_SIGNTOOL`: the path to `signtool.exe`. Without it, PATH is searched, then the newest Windows SDK build.

When nothing is set, signing is skipped with a note, so the unsigned dev flow is never blocked. `-NoSign` skips it even when it's set up. This path signs with the certificate holder's identity as the publisher, and doesn't need CI.
