# Slice 1, package R6B: link it, wire the host, boot it

Branch `slice1/integ` (worktree `~/wt-slice-integ`), build slot: the default one (`~/forever-build/build`, option ON).
Written while the work went on; numbers are filled in as they come.

## 1. Build VM

* `~/forever-build/build.sh` had no way to pass a cmake argument. ONE optional environment variable was added,
  `FOREVER_CMAKE_EXTRA` (appended to the cmake line, empty by default); backup `~/forever-build/build.sh.bak-r6b-20261118`.
  The CMake cache is sticky: after an option-ON build the OFF gate passes `FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=OFF`.
* `~/forever-run/setup.sh` backup `setup.sh.bak-r6b-20261118` (the aiplayerbot.conf copy step, section 5).
* Disk: 9 GB free at the start (`df -h /`), no new build directory was created (the default slot's tree is reused).

## 2. Real-build compile errors
