# Release 0.3.2.5 verification

The public build embeds 528 paths / 461 unique payloads. Its five FFX exports
are present. All runtime sections match the September 18 GPU-tested provider
SHA-256 `1b069c0c42dc615ce578c338dc3df4aa88e4b98e95135cd42677fc2fa49da4d2`
after normalizing PE image-base relocations and the export timestamp; debug
sections are excluded. See `binary-equivalence.json`.

Local checks passed: color-mode priority; provider/preset reader queries;
logging opt-in/path selection; all 24 model-11 variants and malformed-input
rejection; deterministic embedding and corrupt/missing-payload rejection;
Windows provider and dispatch-harness compilation.

SPIR-V Tools 1.4.357.0 validates 180 portable shaders, 144 color shaders, and
24 model-11 variants for Vulkan 1.1 and 1.3. All embedded resource hashes match.
The release ZIP contains exactly the provider under `OptiScaler/`, the restored
`OptiScaler_fallback.dll`, `readme.txt`, and the two license files. Extracted
contents are byte-equal to the build inputs. See `package-manifest.json`.

The fallback matches the 0.3.2.2/0.3.2.3 custom DLL and pinned OptiScaler source
`f52646c3e440d3c7dc1a05ce0a77f30ab68f6dfa`. No optimization experiments,
research capture hooks, or forced-portable game build are included.

This is build/package verification against retained GPU-tested runtime code,
not a fresh game run. See `provider/COLOR-SPACES.md` (from the repository root)
for the retained numerical exceptions and hardware-validation boundaries.
