# Contributing

Bug reports with the problem file attached are the most useful contribution: a VTF that gets a
wrong or no thumbnail can usually be turned into a test right away.

## Code changes

- Build: see *Building* in [README.md](README.md) (Visual Studio, or `./build.sh` with MinGW).
- Run the tests before a pull request:
  ```sh
  pip install srctools pillow
  tests/run_tests.sh            # thumbnail code vs srctools + Pillow, fuzzing (ASan/UBSan)
  tests/run_tests.sh --wine     # + the DLL under Wine, driven like Explorer; regsvr32
  ```
- `src/Thumbnail.*` and `src/vtf/` don't use Windows headers — keep it that way, so they stay
  testable on any platform. Only `Dll.cpp` and `ThumbnailProvider.cpp` are Windows code.
- The DLL runs inside Explorer: a crash there takes Explorer (or the thumbnail process) down.
  Treat every byte of the file as untrusted, check sizes before allocating or reading, and add a
  case to `tests/validate.py` (or a sample to the fuzzer) for anything you fix.
- Please describe what you tested in real Explorer — Wine checks the COM plumbing, Windows is
  the final word.

### Testing a build in Explorer

Explorer keeps the DLL loaded, so to replace it: `regsvr32 /u` the old one, restart Explorer
(Task Manager → Windows Explorer → Restart), copy the new DLL, `regsvr32` it, and clear the
thumbnail cache (*Disk Cleanup → Thumbnails*) so old thumbnails are regenerated.

## Releases (maintainers)

1. Bump the version in `src/Version.h` (`VTFTHUMB_VERSION_MAJOR/MINOR/PATCH`) and add a
   `## <version>` section to `CHANGELOG.md`. Commit (the pencil icon on github.com works too).
2. Start the release, either way:
   - github.com → *Releases* → *Draft a new release* → *Choose a tag* → type `v<version>` →
     *Create new tag* → *Publish release* (leave the description empty to use the changelog);
   - or `git tag v<version>` → `git push origin v<version>`.
3. The *release* workflow (tab *Actions*, ~3 minutes) builds Release | x64 with Visual Studio,
   runs `tools/make_release.ps1` and attaches `VTFThumbnail-<version>.zip` (DLL, install /
   uninstall scripts, README, license) to the release.

If it fails, open the run in *Actions*: the usual cause is a tag that doesn't match
`src/Version.h`. Fix the version, delete the release and the tag, and publish again.
