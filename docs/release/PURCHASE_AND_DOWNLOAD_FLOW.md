# ANDOR Analog — first purchase and download flow (specification)

**Status:** specification with acceptance tests. **Nothing here has been executed.** No store
account, payment provider or download host exists in either repository, and only the owner can
create them. The flow is designed so that it needs no changes to the plug-in: version 1.0 has no
licence activation, and none is added here (see "Copy protection" below).

## 1. Decisions the owner makes first

| Decision | Recommendation | Why |
|---|---|---|
| Seller of record | A merchant-of-record store (Paddle, FastSpring or Lemon Squeezy) | They collect and remit VAT and sales tax worldwide and handle refunds and chargebacks. A one-person publisher should not run its own EU VAT MOSS. |
| Price, launch discount | e.g. list price with a Black Friday introductory price until a fixed date | Show the end date; do not use fake countdowns. |
| Bundle with OSP | Only when OSP has its own release candidate (see the scorecard) | Selling a bundle obliges you to ship both. |
| OSP expansion packs | Sold as `.osppack` downloads from the same store; installed in OSP's Library (OSP `docs/release/SOUND_PACKS_SPEC.md`) | Same checkout, receipts and download limits as the installers. |
| Copy protection | None in 1.0 (download links + licence terms) | No activation code exists. Adding it now would be a new, untested subsystem in the audio path's process. Revisit after launch. |
| Download host | The store's file delivery, or a private object-storage bucket with expiring signed URLs | Never link GitHub Actions artifacts to customers: they expire and need a GitHub login. |
| Support channel | One address on the receipt, the website and the manual | |

## 2. The customer's path

1. **Product page.** Price, system requirements (macOS 11+, Apple silicon and Intel; Windows 10/11
   64-bit; AU/VST3/standalone), sound demos and screenshots from a real DAW, the EULA link and the
   refund policy. A "Buy" button opens the store's checkout overlay.
2. **Checkout** (in the store). Email, payment, the tax shown, acceptance of the EULA (a link to
   the same text as `packaging/common/EULA-DRAFT.txt` once approved).
3. **Receipt email** (the store sends it). It contains the order number, download links for the
   macOS `.pkg` and the Windows `Setup.exe` (each with its SHA-256), a link to the user guide
   (PDF of `docs/MANUAL.md`) and the support address.
4. **Account page** (the store's customer portal). The same links stay available for later
   downloads and updates.
5. **Install.** The signed installer: no Gatekeeper or SmartScreen warning.
6. **First launch.** The factory presets are written to `Documents/ANDOR Analog/Presets/Factory`.
   No serial number or sign-in is asked for.
7. **Updates.** A new version is uploaded to the same product. The store emails owners. The
   installer upgrades in place: same bundle IDs, plugin codes and Windows AppId, so projects and
   presets keep working.

## 3. Release artefacts for each version

Produced by CI from a tag (`v1.0.0`) on the release branch:

| File | From | Must be |
|---|---|---|
| `ANDOR-Analog-1.0.0-macOS.pkg` | `packaging/macos/build-installer.sh` | signed (Developer ID Installer), notarised, stapled |
| `ANDOR-Analog-1.0.0-Windows-Setup.exe` | `packaging/windows/ANDOR-Analog.iss` | Authenticode-signed (setup and the binaries inside it) |
| `*.sha256` | the scripts | published on the account page |
| `ANDOR-Analog-User-Guide-1.0.pdf` | `docs/MANUAL.md` | the version's text |
| `THIRD_PARTY_NOTICES.txt`, `EULA.txt` | inside both installers | as approved |

## 4. Acceptance tests (run before launch; tick only with evidence)

| # | Test | Pass when | Evidence |
|---|---|---|---|
| P1 | Test purchase in the store's sandbox | Receipt email arrives within 5 min with both links and the support address | screenshot of the email |
| P2 | Download links | Both files download from a fresh browser without login (or behind the customer portal login only); SHA-256 matches | `shasum -a 256` output |
| P3 | macOS clean install (a Mac that never had ANDOR) | Installer opens without a Gatekeeper warning; `spctl -a -vv -t install` says "accepted, source=Notarized Developer ID"; Logic lists the AU after rescan; `auval -v aumu Aanl Ospx` passes | terminal output, screenshot |
| P4 | Windows clean install (fresh Windows 11 VM) | No SmartScreen "unknown publisher"; REAPER and Live find the VST3; the standalone starts (no missing DLL) | screenshots |
| P5 | First launch | 44 factory presets listed; a preset plays | screenshot |
| P6 | Upgrade | Install 1.0.0, save a DAW project and a user preset, install the next build over it: the project reopens with identical sound and automation, the user preset is still there | project file, before/after renders |
| P7 | Refund | A sandbox refund revokes access to the portal links (if the store supports it) and sends the refund email | screenshot |
| P8 | Re-download after 30 days | The portal link still works | screenshot |
| P9 | Wrong platform | A Windows user opening the `.pkg` (and the reverse) gets a clear message on the page, not a broken install | screenshot |
| P10 | Support | Mail to the support address is received and answered | email |

## 5. Private beta (before the public launch)

- **Who and what:** invite 10–30 musicians (Logic, Live, REAPER, Cubase, FL Studio users on
  macOS and Windows). Give them the CI installers (unsigned until certificates exist; the macOS
  package says `-UNSIGNED`, and testers right-click › Open) and `docs/release/BETA_TESTER_GUIDE.md`.
- **Reports:** testers report with the template in the guide.
- **Exit criteria:** no open crash or lost-project report, and every DAW in RELEASE.md's list
  covered by at least one tester.
