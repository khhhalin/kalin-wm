# scripts/

Convenience scripts for day-to-day workflow.

## One entry point

- `./scripts/kalin all` → build + unit test
- `./scripts/kalin build` → build
- `./scripts/kalin test` → unit tests
- `./scripts/kalin test-coverage` → unit tests + coverage
- `./scripts/kalin run-nested` → nested compositor run helper
- `./scripts/kalin run-tty [secs]` → tty run (default 30s, `0` disables timeout)

## Direct scripts

- `./scripts/build`
- `./scripts/test [coverage]`
- `./scripts/run-nested`
- `./scripts/run-tty [secs]`

## Development helper scripts

Low-level diagnostics and ad-hoc helpers are grouped under `./scripts/dev/`.

- `./scripts/dev/run-nested-safe.sh`
- `./scripts/dev/run-nested.sh`
- `./scripts/dev/run-with-logging.sh`
- `./scripts/dev/check-crash.sh`
- `./scripts/dev/check-freeze.sh`
- `./scripts/dev/recover-and-check.sh`
- `./scripts/dev/test-no-debug.sh`
- `./scripts/dev/test-spawn.sh`
