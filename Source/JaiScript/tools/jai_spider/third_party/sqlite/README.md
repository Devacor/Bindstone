# SQLite

The SQLite amalgamation, version 3.54.1, exactly as `sqlite-amalgamation-3540100.zip` from
https://www.sqlite.org/download.html ships it (`sqlite3.c`, `sqlite3.h`; the zip's SHA3-256 is
`92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30`). SQLite is in the public domain
(https://www.sqlite.org/copyright.html). Only the `jai_spider` tool compiles it; the JaiScript library does not.

To update: download the new amalgamation zip, check its hash against the download page, replace the two files and
note the version and hash here.
