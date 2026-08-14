# Auto Acquisition Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an operator-configurable auto acquisition feature that starts live DIMM acquisition after local sunset and stops it before the next local sunrise, with lab-test start/stop time override.

**Architecture:** Add `AutoAcquisitionConfig` to the existing `AppConfig`/`QSettings` pipeline, add an "Auto Acquisition" page to `SettingsDialog`, and add a small pure helper for schedule-window calculation. Integrate scheduling from `DIMM::on1hzTick()` and delegate actual acquisition control only through the existing `DIMM::onStartCapture()` and `DIMM::onStopCapture()` paths.

**Tech Stack:** C++17, Qt 6 Widgets, `QSettings`, `QDateTime`, `QTime`, Python static tests with `unittest`.

---

## Guardrails

- Do not call `CameraManager::startAll()`, `CameraManager::stopAll()`, `PulseGeneratorManager`, ROI helpers, or result-file helpers directly from auto acquisition code.
- Auto acquisition may only request live start via `DIMM::onStartCapture()` and live stop via `DIMM::onStopCapture()`.
- Keep all existing capture states and interlocks intact.
- Do not edit unrelated dirty files beyond the files listed in each task.
- Use UTF-8 source text. Existing Chinese strings may render oddly in the terminal; do not rewrite unrelated strings.
- Commit after each task if implementing locally.

## File Structure

- Create `src/AutoAcquisitionScheduler.h`: Pure schedule data types and public helper signatures.
- Create `src/AutoAcquisitionScheduler.cpp`: Test-time window logic, sunset/sunrise calculation, observation-window resolution.
- Modify `src/AppConfig.h`: Add `AutoAcquisitionConfig` and attach it to `AppConfig`.
- Modify `src/AppConfigPersistence.cpp`: Save/load `autoAcquisition/` keys.
- Modify `src/ConfigApplicationController.h/.cpp`: Add callback plumbing for applying auto acquisition config from `SettingsDialog`.
- Modify `src/SettingsDialog.h/.cpp`: Add controls and validation for auto acquisition settings.
- Modify `src/DIMM.h`: Add scheduler state and private methods.
- Modify `src/DIMM.cpp`: Call scheduler from `on1hzTick()`, track manual stop suppression in manual stop paths.
- Modify `src/DIMM.Config.cpp`: Wire settings callback, current config, startup config.
- Modify `CMakeLists.txt`: Explicitly list `AutoAcquisitionScheduler.h/.cpp`.
- Create tests:
  - `tests/test_auto_acquisition_config_static.py`
  - `tests/test_auto_acquisition_settings_static.py`
  - `tests/test_auto_acquisition_scheduler_static.py`
  - `tests/test_auto_acquisition_dimm_static.py`

---

### Task 1: Configuration Model, Persistence, And Callback Plumbing

**Files:**
- Modify: `src/AppConfig.h`
- Modify: `src/AppConfigPersistence.cpp`
- Modify: `src/ConfigApplicationController.h`
- Modify: `src/ConfigApplicationController.cpp`
- Test: `tests/test_auto_acquisition_config_static.py`

- [ ] **Step 1: Write the failing static test**

Create `tests/test_auto_acquisition_config_static.py`:

```python
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AutoAcquisitionConfigStaticTest(unittest.TestCase):
    def test_app_config_declares_auto_acquisition(self):
        header = read("src/AppConfig.h")

        self.assertIn("#include <QTime>", header)
        self.assertIn("struct AutoAcquisitionConfig", header)
        for fragment in [
            "bool enabled = false",
            "double latitudeDeg = 0.0",
            "double longitudeDeg = 0.0",
            "int startOffsetMinutesAfterSunset = 30",
            "int stopOffsetMinutesBeforeSunrise = 30",
            "bool testTimeOverrideEnabled = false",
            "QTime testStartTime = QTime(18, 30)",
            "QTime testStopTime = QTime(6, 0)",
            "AutoAcquisitionConfig autoAcquisition",
        ]:
            self.assertIn(fragment, header)

    def test_qsettings_persists_auto_acquisition_group(self):
        cpp = read("src/AppConfigPersistence.cpp")

        for fragment in [
            "autoAcquisition/enabled",
            "autoAcquisition/latitudeDeg",
            "autoAcquisition/longitudeDeg",
            "autoAcquisition/startOffsetMinutesAfterSunset",
            "autoAcquisition/stopOffsetMinutesBeforeSunrise",
            "autoAcquisition/testTimeOverrideEnabled",
            "autoAcquisition/testStartTime",
            "autoAcquisition/testStopTime",
        ]:
            self.assertIn(f'settings.setValue(QStringLiteral("{fragment}")', cpp)
            self.assertIn(f'settings.value(QStringLiteral("{fragment}")', cpp)

        self.assertIn(".toTime()", cpp)

    def test_config_application_controller_applies_auto_acquisition(self):
        header = read("src/ConfigApplicationController.h")
        cpp = read("src/ConfigApplicationController.cpp")

        self.assertIn("std::function<void(const AutoAcquisitionConfig& config)> applyAutoAcquisition", header)
        self.assertIn("callbacks.applyAutoAcquisition", cpp)
        self.assertIn("callbacks.applyAutoAcquisition(config.autoAcquisition)", cpp)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test and verify it fails**

Run:

```powershell
python -m unittest tests.test_auto_acquisition_config_static
```

Expected: FAIL because `AutoAcquisitionConfig`, persistence keys, and callback plumbing do not exist.

- [ ] **Step 3: Add `AutoAcquisitionConfig`**

In `src/AppConfig.h`, add this include with the existing includes:

```cpp
#include <QTime>
```

Add this struct after `PulseGeneratorConfig` and before `NetworkConfig`:

```cpp
struct AutoAcquisitionConfig {
    bool enabled = false;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
    int startOffsetMinutesAfterSunset = 30;
    int stopOffsetMinutesBeforeSunrise = 30;
    bool testTimeOverrideEnabled = false;
    QTime testStartTime = QTime(18, 30);
    QTime testStopTime = QTime(6, 0);
};
```

Add this field in `struct AppConfig`, after `PulseGeneratorConfig pulseGenerator;`:

```cpp
    AutoAcquisitionConfig autoAcquisition;
```

- [ ] **Step 4: Persist the new config group**

In `src/AppConfigPersistence.cpp`, in `saveSimpleGroups()`, add this block after the pulse generator settings and before network settings:

```cpp
    settings.setValue(QStringLiteral("autoAcquisition/enabled"), config.autoAcquisition.enabled);
    settings.setValue(QStringLiteral("autoAcquisition/latitudeDeg"), config.autoAcquisition.latitudeDeg);
    settings.setValue(QStringLiteral("autoAcquisition/longitudeDeg"), config.autoAcquisition.longitudeDeg);
    settings.setValue(QStringLiteral("autoAcquisition/startOffsetMinutesAfterSunset"),
                      config.autoAcquisition.startOffsetMinutesAfterSunset);
    settings.setValue(QStringLiteral("autoAcquisition/stopOffsetMinutesBeforeSunrise"),
                      config.autoAcquisition.stopOffsetMinutesBeforeSunrise);
    settings.setValue(QStringLiteral("autoAcquisition/testTimeOverrideEnabled"),
                      config.autoAcquisition.testTimeOverrideEnabled);
    settings.setValue(QStringLiteral("autoAcquisition/testStartTime"),
                      config.autoAcquisition.testStartTime);
    settings.setValue(QStringLiteral("autoAcquisition/testStopTime"),
                      config.autoAcquisition.testStopTime);
```

In `loadSimpleGroups()`, add this block after loading pulse generator settings and before network settings:

```cpp
    target.autoAcquisition.enabled =
        settings.value(QStringLiteral("autoAcquisition/enabled"),
                       defaults.autoAcquisition.enabled).toBool();
    target.autoAcquisition.latitudeDeg =
        settings.value(QStringLiteral("autoAcquisition/latitudeDeg"),
                       defaults.autoAcquisition.latitudeDeg).toDouble();
    target.autoAcquisition.longitudeDeg =
        settings.value(QStringLiteral("autoAcquisition/longitudeDeg"),
                       defaults.autoAcquisition.longitudeDeg).toDouble();
    target.autoAcquisition.startOffsetMinutesAfterSunset =
        settings.value(QStringLiteral("autoAcquisition/startOffsetMinutesAfterSunset"),
                       defaults.autoAcquisition.startOffsetMinutesAfterSunset).toInt();
    target.autoAcquisition.stopOffsetMinutesBeforeSunrise =
        settings.value(QStringLiteral("autoAcquisition/stopOffsetMinutesBeforeSunrise"),
                       defaults.autoAcquisition.stopOffsetMinutesBeforeSunrise).toInt();
    target.autoAcquisition.testTimeOverrideEnabled =
        settings.value(QStringLiteral("autoAcquisition/testTimeOverrideEnabled"),
                       defaults.autoAcquisition.testTimeOverrideEnabled).toBool();
    target.autoAcquisition.testStartTime =
        settings.value(QStringLiteral("autoAcquisition/testStartTime"),
                       defaults.autoAcquisition.testStartTime).toTime();
    target.autoAcquisition.testStopTime =
        settings.value(QStringLiteral("autoAcquisition/testStopTime"),
                       defaults.autoAcquisition.testStopTime).toTime();
```

- [ ] **Step 5: Add config-application callback plumbing**

In `src/ConfigApplicationController.h`, add this field to `ConfigApplicationCallbacks`, after `applyEnvironmentSensor`:

```cpp
    std::function<void(const AutoAcquisitionConfig& config)> applyAutoAcquisition;
```

In `src/ConfigApplicationController.cpp`, add this block in `applyValidatedConfig()`, after environment sensor and before network:

```cpp
    if (callbacks.applyAutoAcquisition) {
        callbacks.applyAutoAcquisition(config.autoAcquisition);
    }
```

- [ ] **Step 6: Run the test and verify it passes**

Run:

```powershell
python -m unittest tests.test_auto_acquisition_config_static
```

Expected: PASS.

- [ ] **Step 7: Commit**

```powershell
git add src/AppConfig.h src/AppConfigPersistence.cpp src/ConfigApplicationController.h src/ConfigApplicationController.cpp tests/test_auto_acquisition_config_static.py
git commit -m "feat: add auto acquisition config model"
```

---

### Task 2: Settings Dialog Controls And Validation

**Files:**
- Modify: `src/SettingsDialog.h`
- Modify: `src/SettingsDialog.cpp`
- Test: `tests/test_auto_acquisition_settings_static.py`

- [ ] **Step 1: Write the failing static test**

Create `tests/test_auto_acquisition_settings_static.py`:

```python
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AutoAcquisitionSettingsStaticTest(unittest.TestCase):
    def test_settings_dialog_declares_auto_acquisition_controls(self):
        header = read("src/SettingsDialog.h")

        self.assertIn("onApplyAutoAcquisition", header)
        for name in [
            "autoAcquisitionEnableCheck",
            "autoAcquisitionLatitudeEdit",
            "autoAcquisitionLongitudeEdit",
            "autoAcquisitionStartOffsetEdit",
            "autoAcquisitionStopOffsetEdit",
            "autoAcquisitionTestOverrideCheck",
            "autoAcquisitionTestStartEdit",
            "autoAcquisitionTestStopEdit",
            "autoAcquisitionNextStartLabel",
            "autoAcquisitionNextStopLabel",
        ]:
            self.assertIn(name, header)

    def test_settings_dialog_builds_auto_acquisition_page(self):
        cpp = read("src/SettingsDialog.cpp")
        ctor = cpp.split("SettingsDialog::SettingsDialog", 1)[1].split(
            "mainLayout->addWidget(m_tabWidget)",
            1,
        )[0]

        for fragment in [
            "autoAcquisitionEnableCheck = new QCheckBox",
            "autoAcquisitionLatitudeEdit = new QLineEdit",
            "autoAcquisitionLongitudeEdit = new QLineEdit",
            "autoAcquisitionStartOffsetEdit = new QLineEdit",
            "autoAcquisitionStopOffsetEdit = new QLineEdit",
            "autoAcquisitionTestOverrideCheck = new QCheckBox",
            "autoAcquisitionTestStartEdit = new QLineEdit",
            "autoAcquisitionTestStopEdit = new QLineEdit",
            "autoAcquisitionNextStartLabel = new QLabel",
            "autoAcquisitionNextStopLabel = new QLabel",
            "addSettingsPage(autoAcquisitionTab",
        ]:
            self.assertIn(fragment, ctor)

    def test_apply_settings_validates_and_applies_auto_acquisition(self):
        cpp = read("src/SettingsDialog.cpp")
        apply_body = cpp.split("bool SettingsDialog::applySettings()", 1)[1].split(
            "void SettingsDialog::addSettingsPage",
            1,
        )[0]

        for fragment in [
            "AutoAcquisitionConfig autoAcquisitionConfig",
            "autoAcquisitionLatitudeEdit->text().toDouble",
            "autoAcquisitionLongitudeEdit->text().toDouble",
            "autoAcquisitionStartOffsetEdit->text().toInt",
            "autoAcquisitionStopOffsetEdit->text().toInt",
            "QTime::fromString(autoAcquisitionTestStartEdit->text().trimmed(), QStringLiteral(\"HH:mm\"))",
            "QTime::fromString(autoAcquisitionTestStopEdit->text().trimmed(), QStringLiteral(\"HH:mm\"))",
            "纬度必须在 -90 到 90 之间",
            "经度必须在 -180 到 180 之间",
            "日落后启动偏移必须在 0 到 240 分钟之间",
            "日出前停止偏移必须在 0 到 240 分钟之间",
            "测试开始时间格式必须为 HH:mm",
            "测试停止时间格式必须为 HH:mm",
            "appConfig.autoAcquisition = autoAcquisitionConfig",
        ]:
            self.assertIn(fragment, apply_body)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test and verify it fails**

Run:

```powershell
python -m unittest tests.test_auto_acquisition_settings_static
```

Expected: FAIL because controls and parsing do not exist.

- [ ] **Step 3: Add declarations to `SettingsDialog.h`**

Add this callback after `onApplyEnvironmentSensor`:

```cpp
    std::function<void(const AutoAcquisitionConfig& config)> onApplyAutoAcquisition;
```

Add these public widget fields near the other settings widgets:

```cpp
    QCheckBox* autoAcquisitionEnableCheck = nullptr;
    QLineEdit* autoAcquisitionLatitudeEdit = nullptr;
    QLineEdit* autoAcquisitionLongitudeEdit = nullptr;
    QLineEdit* autoAcquisitionStartOffsetEdit = nullptr;
    QLineEdit* autoAcquisitionStopOffsetEdit = nullptr;
    QCheckBox* autoAcquisitionTestOverrideCheck = nullptr;
    QLineEdit* autoAcquisitionTestStartEdit = nullptr;
    QLineEdit* autoAcquisitionTestStopEdit = nullptr;
    QLabel* autoAcquisitionNextStartLabel = nullptr;
    QLabel* autoAcquisitionNextStopLabel = nullptr;
```

- [ ] **Step 4: Add the settings page in `SettingsDialog.cpp`**

In the constructor, before the network tab is added or immediately before `mainLayout->addWidget(m_tabWidget);`, add:

```cpp
    auto* autoAcquisitionTab = new QWidget();
    auto* autoAcquisitionLayout = new QVBoxLayout(autoAcquisitionTab);
    autoAcquisitionLayout->setContentsMargins(12, 12, 12, 12);
    autoAcquisitionLayout->setSpacing(14);

    auto* autoAcquisitionGroup = new QGroupBox(QStringLiteral("自动采集"));
    auto* autoAcquisitionForm = new QFormLayout(autoAcquisitionGroup);
    autoAcquisitionForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    autoAcquisitionForm->setFormAlignment(Qt::AlignTop);
    autoAcquisitionForm->setHorizontalSpacing(16);
    autoAcquisitionForm->setVerticalSpacing(12);

    autoAcquisitionEnableCheck = new QCheckBox(QStringLiteral("启用自动采集"));
    autoAcquisitionForm->addRow(autoAcquisitionEnableCheck);

    autoAcquisitionLatitudeEdit = new QLineEdit(QStringLiteral("0.000000"));
    autoAcquisitionLatitudeEdit->setToolTip(QStringLiteral("观测地点纬度，北纬为正，范围 -90 到 90。"));
    autoAcquisitionForm->addRow(QStringLiteral("纬度 (deg):"), autoAcquisitionLatitudeEdit);

    autoAcquisitionLongitudeEdit = new QLineEdit(QStringLiteral("0.000000"));
    autoAcquisitionLongitudeEdit->setToolTip(QStringLiteral("观测地点经度，东经为正，范围 -180 到 180。"));
    autoAcquisitionForm->addRow(QStringLiteral("经度 (deg):"), autoAcquisitionLongitudeEdit);

    autoAcquisitionStartOffsetEdit = new QLineEdit(QStringLiteral("30"));
    autoAcquisitionStartOffsetEdit->setToolTip(QStringLiteral("当地日落后等待多少分钟再自动开始采集。"));
    autoAcquisitionForm->addRow(QStringLiteral("日落后启动 (min):"), autoAcquisitionStartOffsetEdit);

    autoAcquisitionStopOffsetEdit = new QLineEdit(QStringLiteral("30"));
    autoAcquisitionStopOffsetEdit->setToolTip(QStringLiteral("当地日出前提前多少分钟自动停止采集。"));
    autoAcquisitionForm->addRow(QStringLiteral("日出前停止 (min):"), autoAcquisitionStopOffsetEdit);

    autoAcquisitionTestOverrideCheck = new QCheckBox(QStringLiteral("启用测试时间"));
    autoAcquisitionTestOverrideCheck->setToolTip(QStringLiteral("启用后使用下面的固定时间窗口，方便实验室测试。"));
    autoAcquisitionForm->addRow(autoAcquisitionTestOverrideCheck);

    autoAcquisitionTestStartEdit = new QLineEdit(QStringLiteral("18:30"));
    autoAcquisitionTestStartEdit->setToolTip(QStringLiteral("测试开始时间，格式 HH:mm。"));
    autoAcquisitionForm->addRow(QStringLiteral("测试开始:"), autoAcquisitionTestStartEdit);

    autoAcquisitionTestStopEdit = new QLineEdit(QStringLiteral("06:00"));
    autoAcquisitionTestStopEdit->setToolTip(QStringLiteral("测试停止时间，格式 HH:mm。"));
    autoAcquisitionForm->addRow(QStringLiteral("测试停止:"), autoAcquisitionTestStopEdit);

    autoAcquisitionNextStartLabel = new QLabel(QStringLiteral("下次开始: 应用后计算"));
    autoAcquisitionNextStartLabel->setWordWrap(true);
    autoAcquisitionForm->addRow(autoAcquisitionNextStartLabel);

    autoAcquisitionNextStopLabel = new QLabel(QStringLiteral("下次停止: 应用后计算"));
    autoAcquisitionNextStopLabel->setWordWrap(true);
    autoAcquisitionForm->addRow(autoAcquisitionNextStopLabel);

    autoAcquisitionLayout->addWidget(autoAcquisitionGroup);
    autoAcquisitionLayout->addStretch();
    addSettingsPage(autoAcquisitionTab, QStringLiteral("自动采集"));
```

- [ ] **Step 5: Parse and validate settings in `applySettings()`**

In `SettingsDialog::applySettings()`, after the environment sensor config is parsed and before `AppConfig appConfig`, add:

```cpp
    AutoAcquisitionConfig autoAcquisitionConfig;
    autoAcquisitionConfig.enabled = autoAcquisitionEnableCheck && autoAcquisitionEnableCheck->isChecked();
    autoAcquisitionConfig.testTimeOverrideEnabled =
        autoAcquisitionTestOverrideCheck && autoAcquisitionTestOverrideCheck->isChecked();

    autoAcquisitionConfig.latitudeDeg =
        autoAcquisitionLatitudeEdit ? autoAcquisitionLatitudeEdit->text().toDouble(&ok) : 0.0;
    if (!ok || autoAcquisitionConfig.latitudeDeg < -90.0 || autoAcquisitionConfig.latitudeDeg > 90.0) {
        showInvalid(QStringLiteral("纬度必须在 -90 到 90 之间。"));
        return false;
    }

    autoAcquisitionConfig.longitudeDeg =
        autoAcquisitionLongitudeEdit ? autoAcquisitionLongitudeEdit->text().toDouble(&ok) : 0.0;
    if (!ok || autoAcquisitionConfig.longitudeDeg < -180.0 || autoAcquisitionConfig.longitudeDeg > 180.0) {
        showInvalid(QStringLiteral("经度必须在 -180 到 180 之间。"));
        return false;
    }

    autoAcquisitionConfig.startOffsetMinutesAfterSunset =
        autoAcquisitionStartOffsetEdit ? autoAcquisitionStartOffsetEdit->text().toInt(&ok) : 30;
    if (!ok || autoAcquisitionConfig.startOffsetMinutesAfterSunset < 0 ||
        autoAcquisitionConfig.startOffsetMinutesAfterSunset > 240) {
        showInvalid(QStringLiteral("日落后启动偏移必须在 0 到 240 分钟之间。"));
        return false;
    }

    autoAcquisitionConfig.stopOffsetMinutesBeforeSunrise =
        autoAcquisitionStopOffsetEdit ? autoAcquisitionStopOffsetEdit->text().toInt(&ok) : 30;
    if (!ok || autoAcquisitionConfig.stopOffsetMinutesBeforeSunrise < 0 ||
        autoAcquisitionConfig.stopOffsetMinutesBeforeSunrise > 240) {
        showInvalid(QStringLiteral("日出前停止偏移必须在 0 到 240 分钟之间。"));
        return false;
    }

    autoAcquisitionConfig.testStartTime =
        QTime::fromString(autoAcquisitionTestStartEdit ? autoAcquisitionTestStartEdit->text().trimmed()
                                                       : QStringLiteral("18:30"),
                          QStringLiteral("HH:mm"));
    if (!autoAcquisitionConfig.testStartTime.isValid()) {
        showInvalid(QStringLiteral("测试开始时间格式必须为 HH:mm。"));
        return false;
    }

    autoAcquisitionConfig.testStopTime =
        QTime::fromString(autoAcquisitionTestStopEdit ? autoAcquisitionTestStopEdit->text().trimmed()
                                                      : QStringLiteral("06:00"),
                          QStringLiteral("HH:mm"));
    if (!autoAcquisitionConfig.testStopTime.isValid()) {
        showInvalid(QStringLiteral("测试停止时间格式必须为 HH:mm。"));
        return false;
    }
```

In the `AppConfig appConfig` aggregate block, add:

```cpp
    appConfig.autoAcquisition = autoAcquisitionConfig;
```

In the callback aggregate named `configCallbacks`, add:

```cpp
        onApplyAutoAcquisition,
```

Place it in the same order as `ConfigApplicationCallbacks` in `ConfigApplicationController.h`.

- [ ] **Step 6: Run the settings test**

Run:

```powershell
python -m unittest tests.test_auto_acquisition_settings_static
```

Expected: PASS.

- [ ] **Step 7: Commit**

```powershell
git add src/SettingsDialog.h src/SettingsDialog.cpp tests/test_auto_acquisition_settings_static.py
git commit -m "feat: add auto acquisition settings UI"
```

---

### Task 3: Pure Auto Acquisition Schedule Helper

**Files:**
- Create: `src/AutoAcquisitionScheduler.h`
- Create: `src/AutoAcquisitionScheduler.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_auto_acquisition_scheduler_static.py`

- [ ] **Step 1: Write the failing static test**

Create `tests/test_auto_acquisition_scheduler_static.py`:

```python
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AutoAcquisitionSchedulerStaticTest(unittest.TestCase):
    def test_scheduler_files_are_registered(self):
        cmake = read("CMakeLists.txt")
        self.assertIn("src/AutoAcquisitionScheduler.h", cmake)
        self.assertIn("src/AutoAcquisitionScheduler.cpp", cmake)

    def test_scheduler_declares_window_and_public_api(self):
        header = read("src/AutoAcquisitionScheduler.h")
        for fragment in [
            "struct AutoAcquisitionWindow",
            "bool valid = false",
            "QDateTime start",
            "QDateTime stop",
            "QString windowId",
            "QString errorMessage",
            "class AutoAcquisitionScheduler",
            "static AutoAcquisitionWindow resolveWindow",
            "static bool contains",
            "static QString formatWindowPreview",
        ]:
            self.assertIn(fragment, header)

    def test_scheduler_implements_test_override_and_cross_midnight(self):
        cpp = read("src/AutoAcquisitionScheduler.cpp")
        for fragment in [
            "config.testTimeOverrideEnabled",
            "config.testStartTime",
            "config.testStopTime",
            "if (stop <= start)",
            "stop = stop.addDays(1)",
            "start = start.addDays(-1)",
        ]:
            self.assertIn(fragment, cpp)

    def test_scheduler_implements_offline_sun_times(self):
        cpp = read("src/AutoAcquisitionScheduler.cpp")
        for fragment in [
            "calculateSunEvent",
            "const double zenithDeg = 90.833",
            "equationOfTime",
            "solarDeclination",
            "hourAngle",
            "startOffsetMinutesAfterSunset",
            "stopOffsetMinutesBeforeSunrise",
            "No local sunset",
            "No local sunrise",
        ]:
            self.assertIn(fragment, cpp)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test and verify it fails**

Run:

```powershell
python -m unittest tests.test_auto_acquisition_scheduler_static
```

Expected: FAIL because scheduler files do not exist.

- [ ] **Step 3: Create `AutoAcquisitionScheduler.h`**

Create `src/AutoAcquisitionScheduler.h`:

```cpp
#pragma once

#include "AppConfig.h"

#include <QDateTime>
#include <QString>

struct AutoAcquisitionWindow {
    bool valid = false;
    QDateTime start;
    QDateTime stop;
    QString windowId;
    QString errorMessage;
};

class AutoAcquisitionScheduler {
public:
    static AutoAcquisitionWindow resolveWindow(const AutoAcquisitionConfig& config,
                                               const QDateTime& now);
    static bool contains(const AutoAcquisitionWindow& window,
                         const QDateTime& now);
    static QString formatWindowPreview(const AutoAcquisitionWindow& window);

private:
    static AutoAcquisitionWindow resolveTestWindow(const AutoAcquisitionConfig& config,
                                                   const QDateTime& now);
    static AutoAcquisitionWindow resolveSunWindow(const AutoAcquisitionConfig& config,
                                                  const QDateTime& now);
};
```

- [ ] **Step 4: Create `AutoAcquisitionScheduler.cpp`**

Create `src/AutoAcquisitionScheduler.cpp` with this implementation. Keep helper functions in an anonymous namespace:

```cpp
#include "AutoAcquisitionScheduler.h"

#include <QtMath>

namespace {

constexpr double kPi = 3.14159265358979323846;

double degToRad(double deg)
{
    return deg * kPi / 180.0;
}

double radToDeg(double rad)
{
    return rad * 180.0 / kPi;
}

double normalizeDegrees(double deg)
{
    double value = std::fmod(deg, 360.0);
    if (value < 0.0) {
        value += 360.0;
    }
    return value;
}

QDateTime localDateTimeForTime(const QDate& date, const QTime& time)
{
    return QDateTime(date, time, Qt::LocalTime);
}

QString windowIdForDates(const QDateTime& start, const QDateTime& stop)
{
    return QStringLiteral("%1/%2")
        .arg(start.date().toString(Qt::ISODate), stop.date().toString(Qt::ISODate));
}

bool calculateSunEvent(const QDate& date,
                       double latitudeDeg,
                       double longitudeDeg,
                       bool sunrise,
                       QDateTime* eventTime,
                       QString* errorMessage)
{
    const double zenithDeg = 90.833;
    const int dayOfYear = date.dayOfYear();
    const double gamma =
        2.0 * kPi / 365.0 * (static_cast<double>(dayOfYear) - 1.0 + (sunrise ? 6.0 : 18.0) / 24.0);
    const double equationOfTime =
        229.18 * (0.000075 + 0.001868 * std::cos(gamma) - 0.032077 * std::sin(gamma) -
                  0.014615 * std::cos(2.0 * gamma) - 0.040849 * std::sin(2.0 * gamma));
    const double solarDeclination =
        0.006918 - 0.399912 * std::cos(gamma) + 0.070257 * std::sin(gamma) -
        0.006758 * std::cos(2.0 * gamma) + 0.000907 * std::sin(2.0 * gamma) -
        0.002697 * std::cos(3.0 * gamma) + 0.00148 * std::sin(3.0 * gamma);

    const double latitudeRad = degToRad(latitudeDeg);
    const double cosHourAngle =
        (std::cos(degToRad(zenithDeg)) / (std::cos(latitudeRad) * std::cos(solarDeclination))) -
        (std::tan(latitudeRad) * std::tan(solarDeclination));

    if (cosHourAngle > 1.0) {
        if (errorMessage) {
            *errorMessage = sunrise ? QStringLiteral("No local sunrise for this date")
                                    : QStringLiteral("No local sunset for this date");
        }
        return false;
    }
    if (cosHourAngle < -1.0) {
        if (errorMessage) {
            *errorMessage = sunrise ? QStringLiteral("No local sunrise for this date")
                                    : QStringLiteral("No local sunset for this date");
        }
        return false;
    }

    const double hourAngle =
        sunrise ? radToDeg(std::acos(cosHourAngle)) : -radToDeg(std::acos(cosHourAngle));
    const QDateTime localNoon(date, QTime(12, 0), Qt::LocalTime);
    const int timezoneMinutes = localNoon.offsetFromUtc() / 60;
    const double solarMinutes =
        720.0 - 4.0 * (longitudeDeg + hourAngle) - equationOfTime + timezoneMinutes;
    const int secondsFromMidnight = qRound(solarMinutes * 60.0);
    *eventTime = QDateTime(date, QTime(0, 0), Qt::LocalTime).addSecs(secondsFromMidnight);
    return true;
}

} // namespace

AutoAcquisitionWindow AutoAcquisitionScheduler::resolveWindow(const AutoAcquisitionConfig& config,
                                                              const QDateTime& now)
{
    if (config.testTimeOverrideEnabled) {
        return resolveTestWindow(config, now);
    }
    return resolveSunWindow(config, now);
}

bool AutoAcquisitionScheduler::contains(const AutoAcquisitionWindow& window,
                                        const QDateTime& now)
{
    return window.valid && now >= window.start && now < window.stop;
}

QString AutoAcquisitionScheduler::formatWindowPreview(const AutoAcquisitionWindow& window)
{
    if (!window.valid) {
        return window.errorMessage;
    }
    return QStringLiteral("%1 -> %2")
        .arg(window.start.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
             window.stop.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
}

AutoAcquisitionWindow AutoAcquisitionScheduler::resolveTestWindow(const AutoAcquisitionConfig& config,
                                                                  const QDateTime& now)
{
    AutoAcquisitionWindow window;
    QDateTime start = localDateTimeForTime(now.date(), config.testStartTime);
    QDateTime stop = localDateTimeForTime(now.date(), config.testStopTime);
    if (stop <= start) {
        stop = stop.addDays(1);
        if (now < start) {
            start = start.addDays(-1);
            stop = stop.addDays(-1);
        }
    }
    window.valid = config.testStartTime.isValid() && config.testStopTime.isValid();
    window.start = start;
    window.stop = stop;
    window.windowId = windowIdForDates(start, stop);
    if (!window.valid) {
        window.errorMessage = QStringLiteral("Invalid test auto-acquisition time");
    }
    return window;
}

AutoAcquisitionWindow AutoAcquisitionScheduler::resolveSunWindow(const AutoAcquisitionConfig& config,
                                                                 const QDateTime& now)
{
    AutoAcquisitionWindow window;
    QDate activeDate = now.date();
    QDateTime todaySunrise;
    QString todaySunriseError;
    if (calculateSunEvent(now.date(),
                          config.latitudeDeg,
                          config.longitudeDeg,
                          true,
                          &todaySunrise,
                          &todaySunriseError) &&
        now < todaySunrise) {
        activeDate = now.date().addDays(-1);
    }

    QDateTime sunset;
    QString sunsetError;
    if (!calculateSunEvent(activeDate,
                           config.latitudeDeg,
                           config.longitudeDeg,
                           false,
                           &sunset,
                           &sunsetError)) {
        window.errorMessage = sunsetError;
        return window;
    }

    QDateTime sunrise;
    QString sunriseError;
    if (!calculateSunEvent(activeDate.addDays(1),
                           config.latitudeDeg,
                           config.longitudeDeg,
                           true,
                           &sunrise,
                           &sunriseError)) {
        window.errorMessage = sunriseError;
        return window;
    }

    window.start = sunset.addSecs(config.startOffsetMinutesAfterSunset * 60);
    window.stop = sunrise.addSecs(-config.stopOffsetMinutesBeforeSunrise * 60);
    if (window.stop <= window.start) {
        window.errorMessage = QStringLiteral("Auto-acquisition stop time is not after start time");
        return window;
    }
    window.valid = true;
    window.windowId = windowIdForDates(window.start, window.stop);
    return window;
}
```

- [ ] **Step 5: Register files in `CMakeLists.txt`**

In `list(APPEND srcs`, add:

```cmake
    src/AutoAcquisitionScheduler.h
    src/AutoAcquisitionScheduler.cpp
```

- [ ] **Step 6: Run the scheduler test**

Run:

```powershell
python -m unittest tests.test_auto_acquisition_scheduler_static
```

Expected: PASS.

- [ ] **Step 7: Commit**

```powershell
git add src/AutoAcquisitionScheduler.h src/AutoAcquisitionScheduler.cpp CMakeLists.txt tests/test_auto_acquisition_scheduler_static.py
git commit -m "feat: add auto acquisition scheduler"
```

---

### Task 4: DIMM Configuration Integration

**Files:**
- Modify: `src/DIMM.h`
- Modify: `src/DIMM.Config.cpp`
- Test: `tests/test_auto_acquisition_dimm_static.py`

- [ ] **Step 1: Write the first DIMM static test**

Create `tests/test_auto_acquisition_dimm_static.py`:

```python
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AutoAcquisitionDimmStaticTest(unittest.TestCase):
    def test_dimm_declares_auto_acquisition_config_and_methods(self):
        header = read("src/DIMM.h")

        for fragment in [
            "AutoAcquisitionConfig m_autoAcquisitionConfig",
            "void setupAutoAcquisitionSettingsCallbacks()",
            "void evaluateAutoAcquisitionSchedule()",
            "void setAutoAcquisitionStatus",
            "void noteManualAutoAcquisitionStopIfNeeded()",
            "bool m_autoAcquisitionCommandInProgress = false",
            "bool m_autoAcquisitionStartedCurrentRun = false",
            "QString m_autoAcquisitionActiveWindowId",
            "QString m_autoAcquisitionSuppressedWindowId",
            "qint64 m_lastAutoAcquisitionAttemptMs = -1",
        ]:
            self.assertIn(fragment, header)

    def test_dimm_config_saves_loads_and_applies_auto_acquisition(self):
        config = read("src/DIMM.Config.cpp")

        for fragment in [
            '#include "AutoAcquisitionScheduler.h"',
            "setupAutoAcquisitionSettingsCallbacks();",
            "void DIMM::setupAutoAcquisitionSettingsCallbacks()",
            "m_settingsDialog->onApplyAutoAcquisition",
            "m_autoAcquisitionConfig = config",
            "config.autoAcquisition = m_autoAcquisitionConfig",
            "m_autoAcquisitionConfig = config.autoAcquisition",
            "autoAcquisitionEnableCheck->setChecked",
            "autoAcquisitionLatitudeEdit->setText",
            "autoAcquisitionLongitudeEdit->setText",
            "autoAcquisitionNextStartLabel->setText",
            "AutoAcquisitionScheduler::resolveWindow",
        ]:
            self.assertIn(fragment, config)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test and verify it fails**

Run:

```powershell
python -m unittest tests.test_auto_acquisition_dimm_static
```

Expected: FAIL because DIMM integration does not exist.

- [ ] **Step 3: Add declarations and runtime state to `DIMM.h`**

Add the include near the other local includes:

```cpp
#include "AutoAcquisitionScheduler.h"
```

Add method declarations near the other settings callback declarations:

```cpp
    void setupAutoAcquisitionSettingsCallbacks();
```

Add method declarations near `on1hzTick()`:

```cpp
    void evaluateAutoAcquisitionSchedule();
    void setAutoAcquisitionStatus(const QString& text,
                                  UiStatusLevel level,
                                  const QString& throttleKey = QString());
    void noteManualAutoAcquisitionStopIfNeeded();
```

Add runtime fields near `m_pulseGeneratorRemoteControl` or near other config fields:

```cpp
    AutoAcquisitionConfig m_autoAcquisitionConfig;
    bool m_autoAcquisitionCommandInProgress = false;
    bool m_autoAcquisitionStartedCurrentRun = false;
    QString m_autoAcquisitionActiveWindowId;
    QString m_autoAcquisitionSuppressedWindowId;
    qint64 m_lastAutoAcquisitionAttemptMs = -1;
    QString m_lastAutoAcquisitionStatusKey;
    qint64 m_lastAutoAcquisitionStatusMs = -1;
```

- [ ] **Step 4: Wire settings callback setup**

In `src/DIMM.Config.cpp`, include the scheduler helper:

```cpp
#include "AutoAcquisitionScheduler.h"
```

In `DIMM::setupSettingsCallbacks()`, add this after `setupPulseGeneratorSettingsCallbacks();`:

```cpp
    setupAutoAcquisitionSettingsCallbacks();
```

Add this function after `setupPulseGeneratorSettingsCallbacks()`:

```cpp
void DIMM::setupAutoAcquisitionSettingsCallbacks()
{
    m_settingsDialog->onApplyAutoAcquisition = [this](const AutoAcquisitionConfig& config) {
        const bool wasEnabled = m_autoAcquisitionConfig.enabled;
        m_autoAcquisitionConfig = config;
        if (!wasEnabled && config.enabled) {
            m_autoAcquisitionSuppressedWindowId.clear();
        }
        const AutoAcquisitionWindow window =
            AutoAcquisitionScheduler::resolveWindow(m_autoAcquisitionConfig, QDateTime::currentDateTime());
        if (m_settingsDialog->autoAcquisitionNextStartLabel) {
            m_settingsDialog->autoAcquisitionNextStartLabel->setText(
                window.valid
                    ? QStringLiteral("下次开始: %1").arg(window.start.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                    : QStringLiteral("下次开始: %1").arg(window.errorMessage));
        }
        if (m_settingsDialog->autoAcquisitionNextStopLabel) {
            m_settingsDialog->autoAcquisitionNextStopLabel->setText(
                window.valid
                    ? QStringLiteral("下次停止: %1").arg(window.stop.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                    : QStringLiteral("下次停止: %1").arg(window.errorMessage));
        }
        setStatusMessage(config.enabled ? QStringLiteral("自动采集已启用")
                                        : QStringLiteral("自动采集已关闭"),
                         config.enabled ? UiStatusLevel::Success : UiStatusLevel::Warning);
    };
}
```

- [ ] **Step 5: Include auto acquisition in current and startup config**

In `DIMM::currentAppConfig()`, add:

```cpp
    config.autoAcquisition = m_autoAcquisitionConfig;
```

In `DIMM::applyStartupConfig(const AppConfig& config)`, add:

```cpp
    m_autoAcquisitionConfig = config.autoAcquisition;
```

After the existing startup code populates other `m_settingsDialog` fields, set the new widgets. If there is not a single existing block for this, add the following near other settings-dialog startup field assignments:

```cpp
    if (m_settingsDialog) {
        if (m_settingsDialog->autoAcquisitionEnableCheck) {
            m_settingsDialog->autoAcquisitionEnableCheck->setChecked(m_autoAcquisitionConfig.enabled);
        }
        if (m_settingsDialog->autoAcquisitionLatitudeEdit) {
            m_settingsDialog->autoAcquisitionLatitudeEdit->setText(
                QString::number(m_autoAcquisitionConfig.latitudeDeg, 'f', 6));
        }
        if (m_settingsDialog->autoAcquisitionLongitudeEdit) {
            m_settingsDialog->autoAcquisitionLongitudeEdit->setText(
                QString::number(m_autoAcquisitionConfig.longitudeDeg, 'f', 6));
        }
        if (m_settingsDialog->autoAcquisitionStartOffsetEdit) {
            m_settingsDialog->autoAcquisitionStartOffsetEdit->setText(
                QString::number(m_autoAcquisitionConfig.startOffsetMinutesAfterSunset));
        }
        if (m_settingsDialog->autoAcquisitionStopOffsetEdit) {
            m_settingsDialog->autoAcquisitionStopOffsetEdit->setText(
                QString::number(m_autoAcquisitionConfig.stopOffsetMinutesBeforeSunrise));
        }
        if (m_settingsDialog->autoAcquisitionTestOverrideCheck) {
            m_settingsDialog->autoAcquisitionTestOverrideCheck->setChecked(
                m_autoAcquisitionConfig.testTimeOverrideEnabled);
        }
        if (m_settingsDialog->autoAcquisitionTestStartEdit) {
            m_settingsDialog->autoAcquisitionTestStartEdit->setText(
                m_autoAcquisitionConfig.testStartTime.toString(QStringLiteral("HH:mm")));
        }
        if (m_settingsDialog->autoAcquisitionTestStopEdit) {
            m_settingsDialog->autoAcquisitionTestStopEdit->setText(
                m_autoAcquisitionConfig.testStopTime.toString(QStringLiteral("HH:mm")));
        }
        const AutoAcquisitionWindow window =
            AutoAcquisitionScheduler::resolveWindow(m_autoAcquisitionConfig, QDateTime::currentDateTime());
        if (m_settingsDialog->autoAcquisitionNextStartLabel) {
            m_settingsDialog->autoAcquisitionNextStartLabel->setText(
                window.valid
                    ? QStringLiteral("下次开始: %1").arg(window.start.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                    : QStringLiteral("下次开始: %1").arg(window.errorMessage));
        }
        if (m_settingsDialog->autoAcquisitionNextStopLabel) {
            m_settingsDialog->autoAcquisitionNextStopLabel->setText(
                window.valid
                    ? QStringLiteral("下次停止: %1").arg(window.stop.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                    : QStringLiteral("下次停止: %1").arg(window.errorMessage));
        }
    }
```

- [ ] **Step 6: Run the DIMM config test**

Run:

```powershell
python -m unittest tests.test_auto_acquisition_dimm_static
```

Expected: PASS for the two tests currently in the file.

- [ ] **Step 7: Commit**

```powershell
git add src/DIMM.h src/DIMM.Config.cpp tests/test_auto_acquisition_dimm_static.py
git commit -m "feat: wire auto acquisition config into dimm"
```

---

### Task 5: DIMM Scheduler Runtime Integration

**Files:**
- Modify: `src/DIMM.cpp`
- Modify: `tests/test_auto_acquisition_dimm_static.py`

- [ ] **Step 1: Extend the DIMM static test**

Append these tests to `AutoAcquisitionDimmStaticTest` in `tests/test_auto_acquisition_dimm_static.py`:

```python
    def test_dimm_evaluates_auto_acquisition_from_1hz_tick(self):
        cpp = read("src/DIMM.cpp")
        tick_body = cpp.split("void DIMM::on1hzTick()", 1)[1].split(
            "void DIMM::matchRoiTimeSlot()",
            1,
        )[0]

        self.assertIn("evaluateAutoAcquisitionSchedule();", tick_body)
        self.assertIn("void DIMM::evaluateAutoAcquisitionSchedule()", cpp)
        scheduler_body = cpp.split("void DIMM::evaluateAutoAcquisitionSchedule()", 1)[1].split(
            "void DIMM::setAutoAcquisitionStatus",
            1,
        )[0]

        for fragment in [
            "AutoAcquisitionScheduler::resolveWindow",
            "AutoAcquisitionScheduler::contains",
            "m_autoAcquisitionSuppressedWindowId == window.windowId",
            "m_lastAutoAcquisitionAttemptMs",
            "canStartLiveCapture(&reason)",
            "m_autoAcquisitionCommandInProgress = true",
            "onStartCapture();",
            "onStopCapture();",
            "m_autoAcquisitionStartedCurrentRun = true",
            "m_autoAcquisitionStartedCurrentRun = false",
        ]:
            self.assertIn(fragment, scheduler_body)

    def test_manual_stop_suppresses_same_auto_window(self):
        cpp = read("src/DIMM.cpp")

        self.assertIn("void DIMM::noteManualAutoAcquisitionStopIfNeeded()", cpp)
        manual_body = cpp.split("void DIMM::noteManualAutoAcquisitionStopIfNeeded()", 1)[1].split(
            "void DIMM::onStartCapture()",
            1,
        )[0]
        for fragment in [
            "m_autoAcquisitionCommandInProgress",
            "m_autoAcquisitionStartedCurrentRun",
            "m_autoAcquisitionSuppressedWindowId = m_autoAcquisitionActiveWindowId",
            "m_autoAcquisitionStartedCurrentRun = false",
        ]:
            self.assertIn(fragment, manual_body)

        start_live_branch = cpp.split("if (m_captureState == CaptureState::Live)", 1)[1].split(
            "if (m_captureState == CaptureState::Simulation)",
            1,
        )[0]
        stop_body = cpp.split("void DIMM::onStopCapture()", 1)[1].split(
            "void DIMM::onShowMainPage()",
            1,
        )[0]
        self.assertIn("noteManualAutoAcquisitionStopIfNeeded();", start_live_branch)
        self.assertIn("noteManualAutoAcquisitionStopIfNeeded();", stop_body)

    def test_auto_acquisition_status_is_throttled(self):
        cpp = read("src/DIMM.cpp")
        status_body = cpp.split("void DIMM::setAutoAcquisitionStatus", 1)[1].split(
            "void DIMM::noteManualAutoAcquisitionStopIfNeeded()",
            1,
        )[0]
        self.assertIn("m_lastAutoAcquisitionStatusKey == throttleKey", status_body)
        self.assertIn("m_lastAutoAcquisitionStatusMs", status_body)
        self.assertIn("setStatusMessage(text, level)", status_body)
```

- [ ] **Step 2: Run the extended test and verify it fails**

Run:

```powershell
python -m unittest tests.test_auto_acquisition_dimm_static
```

Expected: FAIL because runtime scheduler methods do not exist.

- [ ] **Step 3: Call scheduler from `on1hzTick()`**

In `src/DIMM.cpp`, at the end of `DIMM::on1hzTick()` before the closing brace, add:

```cpp
    evaluateAutoAcquisitionSchedule();
```

- [ ] **Step 4: Add status throttling helper**

Add this function before `DIMM::onStartCapture()`:

```cpp
void DIMM::setAutoAcquisitionStatus(const QString& text,
                                    UiStatusLevel level,
                                    const QString& throttleKey)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!throttleKey.isEmpty() &&
        m_lastAutoAcquisitionStatusKey == throttleKey &&
        m_lastAutoAcquisitionStatusMs >= 0 &&
        nowMs - m_lastAutoAcquisitionStatusMs < 60000) {
        return;
    }
    m_lastAutoAcquisitionStatusKey = throttleKey;
    m_lastAutoAcquisitionStatusMs = nowMs;
    setStatusMessage(text, level);
}
```

- [ ] **Step 5: Add manual stop suppression helper**

Add this function after `setAutoAcquisitionStatus()`:

```cpp
void DIMM::noteManualAutoAcquisitionStopIfNeeded()
{
    if (m_autoAcquisitionCommandInProgress ||
        !m_autoAcquisitionStartedCurrentRun ||
        m_autoAcquisitionActiveWindowId.isEmpty()) {
        return;
    }
    m_autoAcquisitionSuppressedWindowId = m_autoAcquisitionActiveWindowId;
    m_autoAcquisitionStartedCurrentRun = false;
    m_autoAcquisitionActiveWindowId.clear();
    setAutoAcquisitionStatus(QStringLiteral("自动采集已手动停止，本观测窗口不再自动重启"),
                             UiStatusLevel::Warning,
                             QStringLiteral("manual-stop-suppression"));
}
```

- [ ] **Step 6: Mark manual stop paths**

In `DIMM::onStartCapture()`, inside the branch:

```cpp
    if (m_captureState == CaptureState::Live) {
```

add this line before `stopLiveCapture();`:

```cpp
        noteManualAutoAcquisitionStopIfNeeded();
```

In `DIMM::onStopCapture()`, add this after the alignment-mode branch and before `stopLiveCapture();`:

```cpp
    noteManualAutoAcquisitionStopIfNeeded();
```

- [ ] **Step 7: Add schedule evaluation**

Add this function before `setAutoAcquisitionStatus()`:

```cpp
void DIMM::evaluateAutoAcquisitionSchedule()
{
    if (!m_autoAcquisitionConfig.enabled) {
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const AutoAcquisitionWindow window =
        AutoAcquisitionScheduler::resolveWindow(m_autoAcquisitionConfig, now);
    if (!window.valid) {
        setAutoAcquisitionStatus(QStringLiteral("自动采集计划不可用: %1").arg(window.errorMessage),
                                 UiStatusLevel::Warning,
                                 QStringLiteral("invalid-window"));
        return;
    }

    const bool insideWindow = AutoAcquisitionScheduler::contains(window, now);
    if (!insideWindow) {
        if (m_autoAcquisitionStartedCurrentRun && m_captureState == CaptureState::Live) {
            m_autoAcquisitionCommandInProgress = true;
            onStopCapture();
            m_autoAcquisitionCommandInProgress = false;
            m_autoAcquisitionStartedCurrentRun = false;
            m_autoAcquisitionActiveWindowId.clear();
            setAutoAcquisitionStatus(QStringLiteral("自动采集已按计划停止"),
                                     UiStatusLevel::Success,
                                     QStringLiteral("auto-stop"));
        }
        if (m_autoAcquisitionActiveWindowId != window.windowId) {
            m_autoAcquisitionActiveWindowId.clear();
        }
        return;
    }

    if (m_autoAcquisitionSuppressedWindowId == window.windowId) {
        setAutoAcquisitionStatus(QStringLiteral("自动采集本窗口已被手动停止，等待下一观测窗口"),
                                 UiStatusLevel::Warning,
                                 QStringLiteral("suppressed-window"));
        return;
    }

    if (m_captureState == CaptureState::Live) {
        return;
    }

    if (m_captureState == CaptureState::Alignment || m_captureState == CaptureState::Simulation) {
        setAutoAcquisitionStatus(QStringLiteral("自动采集等待当前模式结束"),
                                 UiStatusLevel::Warning,
                                 QStringLiteral("blocked-mode"));
        return;
    }

    const qint64 nowMs = now.toMSecsSinceEpoch();
    if (m_lastAutoAcquisitionAttemptMs >= 0 &&
        nowMs - m_lastAutoAcquisitionAttemptMs < 60000) {
        return;
    }
    m_lastAutoAcquisitionAttemptMs = nowMs;

    QString reason;
    if (!canStartLiveCapture(&reason)) {
        setAutoAcquisitionStatus(reason.isEmpty()
                                     ? QStringLiteral("自动采集等待相机连接")
                                     : QStringLiteral("自动采集等待: %1").arg(reason),
                                 UiStatusLevel::Warning,
                                 QStringLiteral("waiting-start-readiness"));
        return;
    }

    m_autoAcquisitionCommandInProgress = true;
    onStartCapture();
    m_autoAcquisitionCommandInProgress = false;

    if (m_captureState == CaptureState::Live) {
        m_autoAcquisitionStartedCurrentRun = true;
        m_autoAcquisitionActiveWindowId = window.windowId;
        setAutoAcquisitionStatus(QStringLiteral("自动采集已按计划启动"),
                                 UiStatusLevel::Success,
                                 QStringLiteral("auto-start"));
    }
}
```

- [ ] **Step 8: Run the DIMM static test**

Run:

```powershell
python -m unittest tests.test_auto_acquisition_dimm_static
```

Expected: PASS.

- [ ] **Step 9: Commit**

```powershell
git add src/DIMM.cpp tests/test_auto_acquisition_dimm_static.py
git commit -m "feat: run auto acquisition scheduler"
```

---

### Task 6: Full Static Test Suite And Build Verification

**Files:**
- No new files unless a test reveals a compile or static-check issue.

- [ ] **Step 1: Run the focused auto acquisition tests**

Run:

```powershell
python -m unittest `
  tests.test_auto_acquisition_config_static `
  tests.test_auto_acquisition_settings_static `
  tests.test_auto_acquisition_scheduler_static `
  tests.test_auto_acquisition_dimm_static
```

Expected: all tests PASS.

- [ ] **Step 2: Run the adjacent existing tests**

Run:

```powershell
python -m unittest `
  tests.test_app_config_structs_static `
  tests.test_app_config_persistence_static `
  tests.test_settings_dialog_split_static `
  tests.test_measurement_ui_static `
  tests.test_dimm_config_cpp_split_static
```

Expected: all tests PASS. If a static test checks an old list of config structs or callback names, update that test only to include the new auto acquisition fields while preserving the old assertions.

- [ ] **Step 3: Configure CMake**

Run:

```powershell
cmake -S . -B build-codex-auto-acquisition -G "Visual Studio 17 2022" -A x64
```

Expected: configure completes without CMake source-list or Qt type errors.

- [ ] **Step 4: Build**

Run:

```powershell
cmake --build build-codex-auto-acquisition --config Release
```

Expected: build completes. If the local machine lacks SDK DLLs or vendor headers, record the exact missing dependency and still keep all Python static tests passing.

- [ ] **Step 5: Commit verification fixes**

If Step 1-4 required fixes, commit them:

```powershell
git add src tests CMakeLists.txt
git commit -m "fix: verify auto acquisition integration"
```

If no fixes were needed, do not create an empty commit.

---

### Task 7: Manual Lab Validation Checklist

**Files:**
- No source changes.

- [ ] **Step 1: Configure test-time override**

In the app settings:

```text
Enable auto acquisition: checked
Latitude: the lab or site latitude
Longitude: the lab or site longitude
Enable test time: checked
Test start: current local time + 2 minutes
Test stop: current local time + 5 minutes
```

Expected: settings apply successfully and preview labels show the upcoming start/stop times.

- [ ] **Step 2: Validate auto start**

Before the test start time:

```text
Connect both cameras.
Keep capture state idle.
Wait for the configured test start time.
```

Expected: the app starts live acquisition through the normal live startup path. Status should say auto acquisition started or the usual live acquisition startup message.

- [ ] **Step 3: Validate manual stop suppression**

During the auto-started live run:

```text
Click Stop.
Wait at least 90 seconds while still inside the test window.
```

Expected: live acquisition does not restart in the same test window. Status indicates this window was manually stopped.

- [ ] **Step 4: Validate auto stop**

Repeat Step 1 with a new test window. Do not manually stop after auto start.

Expected: at the configured test stop time, the app stops live acquisition through the normal stop path and closes result files normally.

- [ ] **Step 5: Validate missing-camera retry**

Configure a test window that starts in 2 minutes, then disconnect cameras.

Expected: at the start time, the app does not crash and does not bypass readiness checks. It reports waiting for cameras and retries no more than once per minute.

---

## Self-Review Notes

- Spec coverage: the plan covers config, settings UI, offline sun schedule, test override, scheduler integration, manual stop suppression, status throttling, and verification.
- Scope: one feature with one helper class and existing settings/capture integration. No independent subsystem split is needed.
- Critical implementation invariant: auto acquisition delegates start/stop only through `onStartCapture()` and `onStopCapture()`.
