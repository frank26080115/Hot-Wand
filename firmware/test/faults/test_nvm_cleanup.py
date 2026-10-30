"""Exit cleanup must leave hardware settings erased, even after failed tests."""
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/auto_testing"))
from target_gdb import GdbSession


class FactoryCleanupTests(unittest.TestCase):
    def session(self):
        session = GdbSession(Path("unused.elf"), {})
        session.command = Mock()
        session.reset = Mock()
        session.read_memory = Mock(return_value=b"\xff" * 1024)
        return session

    def test_cleanup_resets_outputs_then_erases_only_settings_and_verifies(self):
        session = self.session()
        session.restore_factory_defaults()
        self.assertEqual(session.command.call_args_list[0].args[0],
                         '-interpreter-exec console "monitor reset halt"')
        self.assertEqual(session.command.call_args_list[1].args[0],
                         '-interpreter-exec console "monitor flash erase_address 0x08007c00 0x400"')
        session.read_memory.assert_called_once_with(0x08007C00, 1024)
        session.reset.assert_called_once()

    def test_unverified_cleanup_fails_instead_of_reporting_defaults(self):
        session = self.session()
        session.factory_defaults_on_exit = True
        session.read_memory.return_value = bytes(1024)
        with self.assertRaisesRegex(RuntimeError, "Factory-default NVM cleanup failed"):
            session.__exit__(None, None, None)
        session.reset.assert_not_called()

    def test_running_cleanup_waits_for_interrupt_before_reset_and_erase(self):
        session = self.session()
        session.running = True
        session.stopped.wait = Mock(return_value=True)

        def command(instruction, **kwargs):
            if instruction.startswith('-interpreter-exec'):
                self.assertFalse(session.running, "Monitor commands require GDB's stopped state")
                session.stopped.wait.assert_called_once_with(timeout=3)

        session.command.side_effect = command
        session.restore_factory_defaults()
        self.assertEqual([call.args[0] for call in session.command.call_args_list], [
            '-exec-interrupt --all',
            '-interpreter-exec console "monitor reset halt"',
            '-interpreter-exec console "monitor flash erase_address 0x08007c00 0x400"',
        ])
        session.read_memory.assert_called_once_with(0x08007C00, 1024)
        session.reset.assert_called_once()

    def test_failed_interrupt_cannot_proceed_to_flash_erase(self):
        session = self.session()
        session.running = True
        session.stopped.wait = Mock(return_value=False)
        with self.assertRaisesRegex(RuntimeError, "did not report the target stopped"):
            session.restore_factory_defaults()
        self.assertEqual([entry.args[0] for entry in session.command.call_args_list], ['-exec-interrupt --all'] * 2)
        session.read_memory.assert_not_called()
        session.reset.assert_not_called()

    def test_cleanup_failure_also_reports_the_original_test_failure(self):
        session = self.session()
        session.factory_defaults_on_exit = True
        session.read_memory.return_value = bytes(1024)
        with self.assertRaisesRegex(RuntimeError, "ADC boot failed.*Factory-default NVM cleanup failed"):
            with session_context(session):
                raise RuntimeError("ADC boot failed")

    def test_test_failure_and_keyboard_interrupt_still_cleanup(self):
        for error in (AssertionError("failed"), KeyboardInterrupt()):
            with self.subTest(error=type(error).__name__):
                session = self.session()
                session.factory_defaults_on_exit = True
                with self.assertRaises(type(error)):
                    with session_context(session):
                        raise error
                session.reset.assert_called_once()

    def test_ordinary_diagnostic_sessions_preserve_settings(self):
        session = self.session()
        session.__exit__(None, None, None)
        session.command.assert_not_called()


# Exercise real __exit__ without opening a debugger connection.
class session_context:
    def __init__(self, session):
        self.session = session

    def __enter__(self):
        return self.session

    def __exit__(self, *args):
        return self.session.__exit__(*args)


if __name__ == "__main__":
    unittest.main()
