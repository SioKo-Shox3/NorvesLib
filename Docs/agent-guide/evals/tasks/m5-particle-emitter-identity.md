# Preserve particle emitter identity across slot reuse

Edit only `ParticleEmitterIdentity.cpp`. Do not edit `ParticleEmitterIdentityTest.cpp`, `CMakeLists.txt`, or `test.ps1`.

Implement `MakeSyntheticEmitterId(index, generation)` so its identity layout is deterministic:

- bit 63 is the synthetic marker;
- bits 32-62 contain the positive low 31 bits of `generation`;
- bits 0-31 contain `index + 1`.

The result must change when either the index or generation changes, including at the maximum accepted generation. Run the supplied check before finishing:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test.ps1
```
