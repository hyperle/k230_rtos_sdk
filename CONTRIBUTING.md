# Working with This Repository

This private fork stores the SDK, component sources, and required third-party
resources in one Git repository. Work from the repository root; component
directories are ordinary tracked directories.

- `dart_control_board`: RT-Smart firmware for Dart V1, with CanMV disabled.
- `micropython`: Dart V1 MicroPython firmware with CanMV and automatic interpreter startup.

Choose the branch and its matching defconfig before building. See [BUILD.md](BUILD.md)
and the [board guide](boards/k230d_rtos_dart_v1/README.md).

Commit and push all related changes together from the root:

```sh
git status
git add <changed-files>
git commit -m "Describe the change"
git push origin HEAD
```

`origin` is the user's repository (`hyperle/k230_rtos_sdk`). Keep upstream remotes
for reference and push changes to `origin`. The ignored local `.repo/` directory
contains Git storage from the original checkout and is not required by new clones.
