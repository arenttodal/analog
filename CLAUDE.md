# CLAUDE.md — working rules for ANDOR ANALOG

ANDOR ANALOG is built on ANDOR OSP's shared code (`external/osp`, the shell and the ANDOR
interface). OSP's own `CLAUDE.md` rules apply here too: real-time safety, determinism,
versioned state, goldens updated only for intentional changes, `docs/STATUS.md` kept current.

- **Features shared by both instruments.** Every new SPACE, ECHO or ARP feature is built in
  the shared code (OSP's `src/engine`, `shared/andor-ui`, `shared/andor-shell`) and ships in
  both ANDOR OSP and ANDOR ANALOG, tested in both. It is not done while one of the two lacks
  it: if it cannot land in both at once, the gap and its plan are listed under NEXT in OSP's
  `docs/STATUS.md`.
