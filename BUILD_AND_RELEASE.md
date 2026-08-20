# WMB+ Build And Release Workflow

This repository now publishes WMB+ beta firmware through GitHub release assets,
not the older website/Cloudflare release folder flow.

Use this document for WMB+ beta releases. The old `build-and-release.ps1` and
`build-and-release.bat` files are legacy helpers from the upstream project and
are not the current WMB+ release path.

## Release Outputs

The supported beta release boards are:

- `esp32s3-xiao`
- `esp32s3-supermini`
- `esp32s3-tinys3d`

The release helper writes artifacts to:

```text
build-output/<version>/
```

For `0.2.0-beta.9`, the published artifact set contains 25 files:

- `build-info-xiao.json`
- `build-info-supermini.json`
- `build-info-tinys3d.json`
- `manifest-xiao.json`
- `manifest-supermini.json`
- `manifest-tinys3d.json`
- `wmb-plus-<version>-sha256.txt`
- board-specific `-app.bin`
- board-specific `-bootloader.bin`
- board-specific `-partitions.bin`
- board-specific `-littlefs.bin`
- board-specific `-factory-minimal.bin`
- board-specific `-factory-full.bin`

## Build Locally

From a clean release commit:

```bash
tools/build-release-assets.sh 0.2.0-beta.9 \
  esp32s3-xiao \
  esp32s3-supermini \
  esp32s3-tinys3d
```

The helper:

- Builds app firmware for each board.
- Builds LittleFS for each board.
- Copies app, LittleFS, bootloader, and partition binaries.
- Creates `factory-minimal.bin` images.
- Creates `factory-full.bin` images.
- Verifies factory-full image sizes.
- Writes ESP Web Tools manifests.
- Writes board build-info JSON.
- Writes `ota/wmb-plus-beta-latest.json`.
- Writes a SHA-256 checksum file for the artifact folder.

## Clean Worktree Build

When the active checkout has unrelated dirty work, build from a temporary clean
worktree at the release commit:

```bash
tmpdir="$(mktemp -d /tmp/wmbp-release.XXXXXX)"
git worktree add --detach "$tmpdir" <release-commit>
cd "$tmpdir"
tools/build-release-assets.sh <version> esp32s3-xiao esp32s3-supermini esp32s3-tinys3d
```

This keeps experimental work out of release artifacts.

## Verify Artifacts

Run:

```bash
cd build-output/<version>
shasum -a 256 -c wmb-plus-<version>-sha256.txt
```

Check these release-blocking details:

- XIAO `factory-full.bin` is 8,388,608 bytes.
- TinyS3[D] `factory-full.bin` is 8,388,608 bytes.
- SuperMini `factory-full.bin` is 4,194,304 bytes.
- `manifest-xiao.json` uses LittleFS offset `0x610000`.
- `manifest-tinys3d.json` uses LittleFS offset `0x610000`.
- `manifest-supermini.json` uses LittleFS offset `0x350000`.
- Every manifest has `new_install_prompt_erase: false`.
- `build-info-*.json` reports the intended commit hash.

## Publish Or Refresh A GitHub Prerelease

Push the release commit and tag:

```bash
git push wmbfork <branch>
git tag -f v<version> <release-commit>
git push --force wmbfork refs/tags/v<version>
```

Then update the release and upload assets:

```bash
gh release edit v<version> \
  --repo danielfcurrie-alt/weighmybru2 \
  --target <full-release-commit-sha> \
  --prerelease

gh release upload v<version> \
  --repo danielfcurrie-alt/weighmybru2 \
  --clobber \
  build-output/<version>/*
```

Only force-move a beta tag when the beta has not been announced broadly or the
user explicitly decides the prerelease should be replaced in place.

## Verify The Published Release

```bash
gh release view v<version> \
  --repo danielfcurrie-alt/weighmybru2 \
  --json tagName,targetCommitish,isPrerelease,isDraft,url,assets
```

Confirm:

- The release is a prerelease.
- The release is not a draft.
- `targetCommitish` is the intended release commit.
- Uploaded asset digests match the local checksum file.
- A direct download of `build-info-xiao.json` reports the intended commit.

## MacBook Artifact Handoff

The MacBook release artifact destination is:

```text
/Users/dan/FrankenBru-workspaces/releases/<version>/
```

Sync artifacts with:

```bash
ssh -i ~/.ssh/crema_local -p 2222 dan@127.0.0.1 \
  'mkdir -p /Users/dan/FrankenBru-workspaces/releases/<version>'

rsync -az -e 'ssh -i ~/.ssh/crema_local -p 2222' \
  build-output/<version>/ \
  dan@127.0.0.1:/Users/dan/FrankenBru-workspaces/releases/<version>/

ssh -i ~/.ssh/crema_local -p 2222 dan@127.0.0.1 \
  'cd /Users/dan/FrankenBru-workspaces/releases/<version> && shasum -a 256 -c wmb-plus-<version>-sha256.txt'
```

Do not sync source or overwrite the MacBook firmware checkout for routine
release flashing. Use prebuilt artifacts unless source sync is explicitly
requested.

## Flashing Guidance

For first installs or partition migrations, use the matching board
`factory-full.bin` at `0x0`.

For app-only beta testing on XIAO with an existing dual-OTA layout, flash the
same app image to both OTA slots if the current boot slot is unknown:

```bash
python3 -m esptool \
  --chip esp32s3 \
  --port /dev/cu.usbmodem1101 \
  --baud 460800 \
  write_flash \
  0x10000 wmb-plus-<version>-xiao-app.bin \
  0x310000 wmb-plus-<version>-xiao-app.bin
```

For XIAO LittleFS-only updates:

```bash
python3 -m esptool \
  --chip esp32s3 \
  --port /dev/cu.usbmodem1101 \
  --baud 460800 \
  write_flash \
  0x610000 wmb-plus-<version>-xiao-littlefs.bin
```

Never run `erase_flash` for normal beta updates. Calibration, WiFi credentials,
and learned settings live in NVS and should be preserved.
