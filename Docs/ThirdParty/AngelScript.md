# AngelScript 2.38.0

NorvesLib vendors the AngelScript 2.38.0 core as an offline static dependency.

- Primary archive: `https://www.angelcode.com/angelscript/sdk/files/angelscript_2.38.0.zip`
- Archive size: `2060096` bytes
- Archive SHA-256: `B33B5DBCDA10317EF67D628353D83246984CE6FCAC102D4DC2AED121EBA52E6F`
- Upstream cross-reference: tag `v2.38.0`, commit `0601da029d846a658bf23f2888e953a45a94450a`
- Commit archive SHA-256: `0c2ed8bfa0bb3ace32efa842ac96ed605d6ad96bb35d8952a4e4c3acae8004bc`
- License: zlib; the original license text is retained in `Library/ThirdParty/angelscript/LICENSE`.

Only AngelScript core headers and sources are retained under `Library/ThirdParty/angelscript/upstream/`. SDK add-ons are neither copied nor built. `UPSTREAM.json` records a per-file byte-size and SHA-256 manifest, which `AngelScriptVendorContractTest` verifies without network access.

The Norves wrapper target is `NorvesThirdParty_AngelScript`. It builds statically and enables MASM for the official x64 MSVC call-function assembly source. Normal CMake configure and build never fetch AngelScript from the network.
