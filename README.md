# CNETD.R4X

`CNETD.R4X` is an independent R4OS diagnostic program implemented in Zig.

## Package

- Version: `0.1.4`
- Image target: `/R4OS/SOFTWARE/TERMINAL/DIAG/CNETD.R4X`
- Image scope: `none`
- Canonical project manifest: `module.R4MF`

The manifest is the single source of truth for the artifact, imports, image
target, and package metadata.

## Build

On Windows:

    Build.bat

On Linux or macOS:

    ./Build.sh

The build starters resolve the current local R4OS dependency checkouts through
`Settings.R4S`. The URL and hash entries in `build.zig.zon` record the
last verified standalone dependency identities; workspace builds use the
mapped local checkouts.

## Documentation

Detailed German technical notes from the migration are preserved in
`DOCUMENTATION.de.txt`. Source-transfer provenance is recorded in
`PROVENANCE.txt`.

## License

Original R4OS material is licensed under Apache License 2.0. See `LICENSE`
and `NOTICE`. Any repository-specific external material is documented in
`THIRD_PARTY_NOTICES.md`.

## Generation 2

The normal C Start2/Query2 entry uses the generated app context, common argument cursor and actual DNS/TCP/UDP family client. It checks provider, socket and generation records, full send acknowledgements and explicit releases. UDP owns local port 65023 because port zero is rejected by R4OS. IPv4 leading zeroes remain decimal; UDP payload suffix bytes are retained. Each operation has a ten-second monotonic budget; resource release has a separate two-second budget and never replays a mutation. DNS lookup, TCP connect and UDP send keep their existing command forms and success/failure exit values. IMAGE_SCOPE remains none.
