"""Debugger stop synchronization across firmware resets; no hardware access."""
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, call

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/auto_testing"))
from target_gdb import GdbSession


class HaltSynchronizationTests(unittest.TestCase):
    def session(self, stops):
        session = GdbSession(Path("unused.elf"), {})
        session.running = True
        session.command = Mock()
        session.stopped = Mock()
        session.stopped.wait.side_effect = stops
        return session

    def test_normal_halt_needs_one_confirmed_interrupt(self):
        session = self.session([True])
        session.halt()
        session.command.assert_called_once_with("-exec-interrupt --all")
        self.assertFalse(session.running)

    def test_reset_consumes_first_interrupt_and_second_confirms_stop(self):
        session = self.session([False, True])
        session.halt()
        self.assertEqual(session.command.call_args_list, [call("-exec-interrupt --all")] * 2)
        self.assertFalse(session.running)

    def test_no_stop_after_bounded_retry_fails_without_reading_memory(self):
        session = self.session([False, False])
        callback = Mock()
        with self.assertRaisesRegex(RuntimeError, "two interrupt attempts"):
            session.inspect(callback)
        callback.assert_not_called()
        self.assertTrue(session.running)
        self.assertEqual(session.command.call_count, 2)

    def test_confirmed_stopped_target_does_not_get_interrupted_again(self):
        session = self.session([])
        session.running = False
        session.halt()
        session.command.assert_not_called()


if __name__ == "__main__":
    unittest.main()
