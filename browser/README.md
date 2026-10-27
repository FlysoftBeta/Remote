# Remotekit Browser (rb)

`PIN.json` pins upstream Chromium. `series` orders the maintenance commits in
`patches/`; `args/` holds the dev and release GN configurations. The product name
shown to users is **PRTS**. Code, switches and the checkout branch use **rb**.

From the repository root on Windows (Git, depot_tools, Python 3 and Visual Studio
must be installed):

```powershell
.\browser\cr.cmd init                    # fetch/sync the pin, create upstream, apply
.\browser\cr.cmd apply                   # replay series on rb, then generate branding
.\browser\cr.cmd brand                   # regenerate UI resources, also safe to repeat
.\browser\cr.cmd check                   # check the series against upstream
.\browser\cr.cmd gen dev                 # preserves existing args unless -Force
.\browser\cr.cmd build dev chrome
.\browser\cr.cmd build dev re
.\browser\cr.cmd run dev                 # --headless=new --rb-remote
.\browser\cr.cmd export                  # export upstream..rb into browser/patches
```

The checkout defaults to `C:\src\chromium\src`; set `CR_ROOT` to its parent to
override it. The legacy endpoint also needs a directory symlink named
`re` in the checkout pointing to this repository's `re/`.
Its executable is `out\dev\re.exe`. Stop the owned test chain
before linking a running executable. Builds are incremental; do not clean outputs.

Commit source changes on `rb`, one maintenance unit per commit, with a unique
`Patch-Name: NN-rb-description` trailer (existing descriptive names without rb
are also valid). Run `cr export`, then commit the exported files in this repository.

`branding/apply_branding.py` rewrites only allowlisted UI message bodies and their
translations, using `branding/config.json`. It preserves attributes, placeholders,
URLs, filenames and machine identifiers. Translation IDs follow the checkout's
GRIT fingerprint algorithm. Run it standalone with Python 3:

```powershell
python3 browser/branding/apply_branding.py --src C:/src/chromium/src --report C:/src/tmp/branding.json
python3 browser/branding/apply_branding.py --src C:/src/chromium/src --restore
```

Generated resource edits remain **uncommitted** in the Chromium working tree.
`cr export` reads commits only and rejects committed changes to these resources;
never include them in a source commit. `cr apply` restores generated resources
before its clean-tree check and regenerates them after replay. The generator
refuses to overwrite manual edits and leaves identical files untouched. Restore
branding before editing these resources or rebasing the checkout.

Patch 12 contains only the product `BRANDING` fields. Resource rewrites are not
part of the series. Patch 13 keeps web-facing UA/UA-CH identity **Google Chrome**
in remote mode; UI branding never rewrites that code.
