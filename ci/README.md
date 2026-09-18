# Making a release

Each release include one Blender add-on's .zip per row of `ci/targets.toml`.

How it works:

- `addon_version` in `ci/targets.toml` is the version. The tag must be exactly `v<addon_version>`.
  If it isn't, the `targets` job fails and nothing gets built.
- Pushing a `v*` tag runs `.github/workflows/blender.yml`. Every row builds, passes `check_so.sh` and
  the smoke test, and is packaged. Then the `release` job creates a **draft** release with all the
  zips. If any row fails, no draft is created.
- Builds without a tag are versioned `<addon_version>-dev+g<sha>`.

## 1. Before tagging

1. Commit everything that should ship, and push `main`.
2. Wait for the `blender` workflow on that push to go green on every row:

   ```bash
   gh run list --workflow blender --branch main --limit 1
   ```

   A tag reruns the same build. A red `main` means a red tag.
3. Check that `blender-watch` has no open drift issue (`gh issue list`). If the watch job found a
   newer Blender patch release, bump that row's pin first (see the comment at the top of
   `ci/targets.toml`) and go back to step 1.
4. Check that `addon_version` in `ci/targets.toml` is the version you are releasing, and that
   neither the tag nor the release exists yet:

   ```bash
   grep ^addon_version ci/targets.toml
   git tag -l 'v*'; git ls-remote --tags origin 'v*'
   ```

5. **Only if the workflow file or `ci/targets.py` changed since the last release:** do the rc dry
   run in `docs/plans/blender-ci.md` ("The release dry run") first. It checks the release path
   without using up the real tag.

## 2. Tag and push

```bash
git switch main && git pull
git tag -a v0.3.0 -m "v0.3.0"
git push origin v0.3.0
```

Then watch the run (you can look at the actions tab in the browser instead):

```bash
gh run list --workflow blender --limit 1          # the tag's run: headBranch is v0.3.0
gh run watch <run-id>
```

You want to see `targets`, four `build` rows and `release` all green.

## 3. Check the draft

In the browser, check under this repo's **Releases** tab.

- The draft is named `v0.3.0` and has one zip per row in `ci/targets.toml` (four today: 4.5, 5.0,
  5.1, 5.2).
- Each filename has `0.3.0` in it, with no `-dev+g` suffix.

## 4. Install-check one zip

Verify the .zip we are distributing works!

```bash
source env.sh                                     # for $BLENDER
unset HDW_PLUGIN_DIR                              # otherwise a broken zip layout still passes
```

Download the correct .zip from the release draft to `weekend_raytracer/dist-ci`

```bash
$BLENDER --command extension validate dist-ci/*.zip
$BLENDER --command extension remove user_default.weekend_raytracer   # previous install
$BLENDER --command extension install-file --repo user_default -e dist-ci/*.zip
$BLENDER -b -E WEEKEND -o /tmp/release_ -F PNG -f 1                  # headless F12, default cube
```

Then open `$BLENDER` and check the Weekend engine with F12 and a Rendered viewport. Afterwards,
remove `dist-ci`.

## 5. Publish

1. Finish writing the changelog or release description.
2. Press **Publish release**.

## 6. After publishing

Bump `addon_version` in `ci/targets.toml` to the next version (e.g. `0.3.1` or `0.4.0`), then
commit and push. Otherwise builds from `main` are named `0.3.0-dev+g<sha>`, and semver sorts that
*before* the release you just published.

## If something goes wrong

- **`targets` fails with `tag 'vX' does not match addon_version`:** the tag and `targets.toml`
  disagree. Delete the tag (`git push origin --delete vX && git tag -d vX`), fix whichever one is
  wrong, and go back to step 2.
- **A `build` row fails:** no draft was made. Delete the tag the same way, fix the problem on `main`,
  wait for green, and tag again.
- **The draft is wrong, or the install check fails:** delete both the draft and the tag with
  `gh release delete vX --yes --cleanup-tag` (this needs a token with write access, or use the
  browser), then `git tag -d vX`. Fix the problem and start again from step 1.

Never move a tag after the release is published. Release a new patch version instead.

