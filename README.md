# aMule pull request screenshot archive

This branch retains the PNG sources embedded in amule-org/amule pull requests #1920, #1921, and #1924.

Keep this branch and the `screenshots/pr-1920-1921-1924` tag. Do not delete, force-push, or repurpose them. The PR descriptions use the full archive commit hash so later additions cannot change existing images.

The screenshots are documentation assets only; this archive is not intended to be merged upstream.

## Verify the evidence

`manifest.json` records the original immutable URLs, SHA-256 digests, dimensions,
source revisions, settings and capture limitations. `SHA256SUMS` can also be
checked with `sha256sum -c SHA256SUMS`. Neither screenshot dimensions nor a
successful build on another revision establish platform or HiDPI acceptance.

```sh
python3 tools/verify.py
# Also fetch each public image and check its bytes, then check live PR embeds:
python3 tools/verify.py --online
```

Online verification requires network access and an authenticated GitHub CLI.
The local check and offline restore need only Git and Python's standard library.
The checker rejects missing, changed, unexpected, or malformed PNGs. New evidence
must be added explicitly to the manifest; never replace an existing capture or
change the original URLs in the PRs merely to follow this branch's current tip.

## Retain and recover

Active repository rules prevent deletion and force-push of this archive branch,
and prevent deletion and updates of its original annotated tag. Keep those
rules enabled. The branch may advance by adding documentation and new evidence;
the original image commit must remain its ancestor.

No URL is guaranteed to remain available forever. Repository deletion, account
loss, permission changes or loss of the hosting service can still remove access.
Retain a complete Git bundle and an ordinary source export outside this checkout:

```sh
git bundle create /absolute/backup/pr-screenshots.bundle \
  refs/heads/docs/pr-screenshot-archive \
  refs/tags/screenshots/pr-1920-1921-1924
git bundle verify /absolute/backup/pr-screenshots.bundle
git archive --format=tar.gz --output=/absolute/backup/pr-screenshots.tar.gz HEAD
```

Use a new backup directory per snapshot rather than overwriting the only copy.
Keep another copy on independent storage, preferably offsite. A second directory
on the same computer protects against checkout removal but not disk or host loss.
Record SHA-256 digests of both backup files alongside them.

Restore and verify without contacting GitHub:

```sh
git clone --branch docs/pr-screenshot-archive /absolute/backup/pr-screenshots.bundle restored
cd restored
python3 tools/verify.py
git merge-base --is-ancestor f6d258996bbc1755aef77218116b1c48dba3cb19 HEAD
```

If public hosting is lost, these bytes can be published at a replacement location
and the PR descriptions repaired. The original URLs themselves cannot be made
independent of GitHub's continued availability.

## Capture provenance

- **#1920:** isolated native bandwidth panel renders, using the panel sources
  from the recorded base and head. These are not full application captures.
- **#1921:** Connection preferences from an existing local Debug application
  with the selected preferences sources recompiled for each comparison. The
  complete application binary revision was not recorded.
- **#1924:** real shutdown splash captured with GDB return breakpoints after
  `CamuleGuiBase::UpdateShutdownStatus`, under Xvfb. The debugger paused stages
  for capture; these images do not measure how long stages normally remain visible.

The original environment and complete capture procedure were not preserved, so
byte-identical regeneration is not promised. The retained PNGs are the original
published sources. For future captures, save the capture command or harness,
complete build revision, dependency versions, display scale, locale, theme and
sanitized fixture settings with the images. Keep credentials, device identifiers,
private profiles and unsanitized captures outside this public archive.
