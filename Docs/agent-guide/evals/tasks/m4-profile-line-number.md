# Remove physical source-line coupling from the approved profile errors

Edit only `CookedModelGameProfileContract.ps1`. Do not edit `test.ps1`.

The three approved TextureResources, SlangCompiler, and ShaderManager error records
currently depend on stale physical C++ source line numbers. Replace only each physical
line-number matcher with a matcher that accepts any positive ASCII decimal physical line
number. Preserve the category, source file, fully-qualified function, message, and path
envelope exactly. Run the supplied check before finishing:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test.ps1
```
