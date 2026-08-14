# MHXX Dex runtime dumper

This tool loads the pinned `MHXX Dex.exe` as a 32-bit .NET assembly and invokes
the same resource/password and database initialization methods used by version
1.0. It exports all live `DataTable` objects and SQLite tables. It does not
modify any file in the Dex directory.

Run on Windows from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_windows.ps1 `
  -DexDir 'D:\MH\DEX\xx图鉴' `
  -OutDir D:\MH\DEX\mhxx-dex-raw
```

The output directory is external research data and must not be committed. On a
repeat run, the runner only replaces a directory containing its own
`.mhxx-dex-dump` marker.

This implementation targets exactly `MHXX Dex 1.0` with SHA-256
`5e0508b80b02565c5c7217861c315911878896567b07659631c34b7f7dae46c7`.
The executable identity, 131-table layout, and obfuscated initialization methods
are verified before invocation; a different build fails closed.
