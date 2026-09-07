# Test assets

Most assets here are committed and redistributable under their own licenses. One is not,
and has to be fetched.

## Kitchen_set — not committed, fetch it yourself

Pixar's USD Kitchen Set is **not in this repository** and cannot be. Its `LICENSE.txt` is an
EULA, not an open license: it grants a revocable, non-transferable licence for "personal,
non-commercial testing" only, and §3(e) says you may not "distribute the USD Kitchen Asset or
any Derivative Works without Pixar's written authorization". Committing it to a public repo is
distribution, so `assets/Kitchen_set/` is in `.gitignore`.

Downloading it for your own use is exactly what the EULA permits. From the repo root:

```bash
curl -O https://openusd.org/files/Kitchen_set.zip
unzip -q Kitchen_set.zip -d assets/ && rm Kitchen_set.zip
```

The archive's top-level directory is `Kitchen_set/`, so this lands it at `assets/Kitchen_set/`
— the path the scene files and docs expect. Verified 2026-09-07: 2,702,173 bytes, 233 files,
which matches what used to be tracked here byte-for-byte.

Check it worked:

```bash
test -f assets/Kitchen_set/Kitchen_set.usd && echo OK
```

`Kitchen_set_instanced.usd` in the same archive is the USD-native-instancing variant, which is
the more interesting one for exercising the delegate's instancing path.

## Committed assets

| Path | License | Attribution |
|---|---|---|
| `StandardShaderBall/` | CC BY 4.0 | Academy Software Foundation |
| `OpenChessSet/` | CC BY 4.0 | Academy Software Foundation |
| `ElephantWithMonochord/` | CC BY-SA 4.0 | "Songs of Cultures – Vietnamese Elephant with Monochord" by [A.MUSE – Interactive Design Studio](https://amuse.vision) |
| `Teapot/` | CC0 | Derived from PolyHaven's [Tea Set](https://polyhaven.com/a/tea_set_01) by James Ray Cook, Jurita Burger and Rico Cilliers |

These are redistributable with attribution, so they stay committed. Note that Blender's
extension platform requires assets *shipped inside* an add-on to be CC0 — none of these ship in
the add-on zip, so that rule does not apply here.
