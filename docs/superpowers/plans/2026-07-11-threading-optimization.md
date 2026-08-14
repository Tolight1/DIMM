# Threading Optimization Implementation Plan

**Goal:** 修复 Qt 相机线程关闭死锁和信号初始化缺陷，并为相机帧入口增加有界背压。

**Architecture:** `CameraManager` 在回调线程只更新每相机 latest-frame 槽位并投递一次 queued 通知；GUI 收到通知后取出最新帧并沿用现有 UI/worker 流程。回调计数使用显式的一次性守卫，关闭流程只等待计数，不在锁内重入收尾。

**Tech Stack:** Qt 6, C++17, QMutex/QWaitCondition/QMetaObject, OpenCV `cv::Mat`, Python source-invariant tests.

---

### Task 1: Add regression invariants

**Files:** `tests/test_threading_invariants.py`

- [x] Assert construction-before-connections, no `finishCallback()` inside the state-lock block, and presence of per-camera pending/latest-frame state.
- [x] Run `python tests/test_threading_invariants.py`; it failed before the fix and passes after it.

### Task 2: Fix initialization and callback lifecycle

**Files:** `src/DIMM.cpp`, `src/CameraManager.cpp`, `src/CameraManager.h`

- [x] Move source-object construction before `setupConnections()`.
- [x] Make callback completion happen outside the state-lock scope.
- [x] Make the second state check record an abort flag, leave the lock, and then complete the callback.

### Task 3: Add bounded latest-frame handoff

**Files:** `src/CameraManager.h`, `src/CameraManager.cpp`, `src/DIMM.cpp`

- [x] Add a per-camera frame mutex, latest frame, and atomic notification-pending flag.
- [x] Publish a frame by replacing the latest slot and queueing at most one GUI notifier.
- [x] Make the GUI handler fetch the latest frame and clear/re-arm the pending flag safely.

### Task 4: Verify

- [x] Run `python tests/test_threading_invariants.py` and confirm PASS.
- [x] Run `git diff --check` (only existing line-ending warnings remain).
- [x] Build delegated to the user; the configured generator is unavailable in this environment.

### Task 5: Remove redundant full-frame copies

**Files:** `src/CameraManager.cpp`, `src/ImageProcessor.cpp`, `tests/test_threading_invariants.py`

- [x] Return the latest `cv::Mat` by reference-counted value from the protected slot.
- [x] Capture the frame into the worker lambda by reference-counted value; the worker only reads it and clones the small ROI.
- [x] Verify the no-full-clone invariant with the source test.
