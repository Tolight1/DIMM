from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class EafShutdownLifecycleStaticTest(unittest.TestCase):
    def test_shutdown_checks_worker_wait_and_marks_timeout(self):
        header = read("src/EafFocuserManager.h")
        source = read("src/EafFocuserManager.cpp")
        shutdown_body = source.split("void EafFocuserManager::shutdown()", 1)[1].split(
            "EafSdkLoader* EafFocuserManager::sdkLoader",
            1,
        )[0]

        self.assertIn("bool m_workerShutdownTimedOut = false", header)
        self.assertIn("doShutdownAndQuit", source)
        self.assertIn("const bool stopped = m_workerThread->wait(", shutdown_body)
        self.assertIn("if (!stopped)", shutdown_body)
        self.assertIn("m_workerShutdownTimedOut = true", shutdown_body)
        self.assertLess(
            shutdown_body.find("if (!stopped)"),
            shutdown_body.find("m_workerThread = nullptr"),
        )
        self.assertNotIn("Qt::BlockingQueuedConnection", shutdown_body)

    def test_destructor_retains_sdk_loader_when_worker_is_still_running(self):
        source = read("src/EafFocuserManager.cpp")
        destructor_body = source.split("EafFocuserManager::~EafFocuserManager()", 1)[1].split(
            "void EafFocuserManager::initialize()",
            1,
        )[0]

        self.assertIn("if (m_workerShutdownTimedOut)", destructor_body)
        timeout_branch = destructor_body.split("if (m_workerShutdownTimedOut)", 1)[1].split(
            "delete m_sdk",
            1,
        )[0]

        self.assertIn("m_workerThread->setParent(nullptr)", timeout_branch)
        self.assertIn("m_sdk = nullptr", timeout_branch)
        self.assertNotIn("delete m_sdk", timeout_branch)


if __name__ == "__main__":
    unittest.main()
