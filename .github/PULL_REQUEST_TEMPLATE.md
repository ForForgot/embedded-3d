## What and why

<!-- What changes, and what problem it solves. -->

## How it was verified

- [ ] `ctest --test-dir build` passes (11 configurations)
- [ ] builds clean with `-Wall -Wextra -Wpedantic -Werror`
- [ ] if the raster output could change: the demo frames were rendered before and
      after and compared (they should be identical unless the change is meant to
      alter the picture)
- [ ] if a new compile time switch was added: it is in the README table and has a
      CTest configuration that exercises it

## Notes for reviewers

<!-- Anything surprising, anything deliberately left out. -->
