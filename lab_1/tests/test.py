"""
This script runs a number of tests against your lsh shell implementation.
If your shell passes all these tests, it's a good sign that it's working
properly, but you should still manually test it yourself.

To run it, set up a venv with the packages in requirements.txt:

  # in the tests directory:
  python3 -m venv .venv
  source .venv/bin/activate
  pip install -r requirements.txt

And then, simply run this script. It will build anew and test an lsh
implementation whose source is in '../code' relative to where you run the script
from. If your lsh code is somewhere else, this default directory can be
overridden by setting the LSH_CODE environment variable. So, your options are:

  # if your lsh code is in the repo's lab_1/code, and you are running this from
  # lab_1/test:
  python test.py

  # if your lsh code is anywhere else:
  LSH_CODE=<your code dir> python test.py

---
Jacob Garby <garby@chalmers.se>
Sept 2026
"""

import pty, os
from time import sleep
from psutil import Process, TimeoutExpired, STATUS_ZOMBIE
from signal import SIGINT
from datetime import datetime
from tempfile import TemporaryDirectory as TempDir, mkdtemp
import select
import psutil
import unittest
from HTMLTestRunner.runner import HTMLTestRunner
import subprocess as sp
from pathlib import Path

LSH_CODE = "../code/"
if "LSH_CODE" in os.environ:
    LSH_CODE = os.environ["LSH_CODE"]
    print(f"(Using custom LSH_CODE from env: {LSH_CODE})")

LSH_CODE = Path(LSH_CODE).resolve()


class Shell:
    def __init__(self, exe):
        self.exe = exe
        self.fd = None
        self.pid = None

    def start(self):
        pid, fd = pty.fork()
        if pid == 0:
            os.execv(self.exe, [self.exe])
            print(f"ERROR: Couldn't run {self.exe}")
            os.exit(1)
        else:
            self.pid = pid
            self.fd = fd
            print(f"Running {self.exe}, pid={self.pid}, tty fd={self.fd}")

    def read_available(self):
        if self.fd is None:
            return b""
        r, _, _ = select.select([self.fd], [], [], 0.5)
        if r:
            try:
                return os.read(self.fd, 65536)
            except OSError:
                return b""
        return b""

    def eof(self, wait_after=0.5):
        if self.fd is None:
            return
        self.read_available()
        print("Sending EOF to lsh")
        os.write(self.fd, b"\x04")
        sleep(wait_after)

    def sendline(self, cmd, wait_after=0.5):
        if self.fd is None:
            return
        self.read_available()
        cmdbytes = bytes(cmd + "\n", "utf-8")
        print(f"Sending cmd '{cmd}' to lsh")
        os.write(self.fd, cmdbytes)
        sleep(wait_after)

    def run_see_cmd(self, cmd, wait=0.5):
        if self.fd is None:
            return ""
        self.sendline(cmd, wait_after=wait)
        return self.read_available().decode()

    def ctrl_c(self, wait_after=0.5):
        if self.fd is None:
            return
        self.read_available()
        print("Sending Ctrl-C to lsh")
        os.write(self.fd, b"\x03")
        sleep(wait_after)

    def children(self):
        if self.pid is None:
            return []
        ret = Process(self.pid).children()
        return ret

    def proc(self):
        return Process(self.pid)

    def alive(self):
        if self.pid is None:
            return False
        return psutil.pid_exists(self.pid)

    def has_zombies(self):
        for ch in self.children():
            if ch.status() == STATUS_ZOMBIE:
                return True
        return False


class TestLsh(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.build_dir = mkdtemp()
        print(f"Building lsh to {cls.build_dir}")
        sp.run(["cmake", "-B", cls.build_dir, "-S", LSH_CODE], check=True)
        sp.run(["cmake", "--build", cls.build_dir], check=True)
        cls.lsh_path = cls.build_dir + "/lsh"
        print(f"lsh built at {cls.lsh_path}")

    @classmethod
    def tearDownClass(cls):
        # Sanity check that build_dir is in /tmp, before rm -rf'ing it
        if cls.build_dir.startswith("/tmp"):
            print(f"\n(Cleaning temporary build dir {cls.build_dir})")
            sp.run(["rm", "-rf", cls.build_dir], check=True)

    def setUp(self):
        print(f"Using lsh at {self.lsh_path}")
        self.lsh = Shell(exe=self.lsh_path)
        self.lsh.start()

    def tearDown(self):
        if self.lsh.alive():
            try:
                p = self.lsh.proc()
                p.kill()
                p.wait(timeout=3)
            except TimeoutExpired:
                print("lsh took too long to shut down")

    def test_exit_command(self):
        """
        Check that 'exit' command terminates the shell.
        """
        proc = self.lsh.proc()
        self.lsh.sendline("exit", wait_after=0)

        try:
            ret = proc.wait(3)
            self.assertEqual(ret, 0, msg="shell should return 0 on exit")
        except TimeoutExpired:
            self.assertTrue(False, msg="'exit' command didn't terminate the shell")

    def test_eof(self):
        """
        Check that EOF (Ctrl-D) correctly terminates the shell.
        """
        proc = self.lsh.proc()

        self.lsh.read_available()
        self.lsh.eof(wait_after=0)

        try:
            ret = proc.wait(timeout=5)
            print(f"wait returned {ret}")
            self.assertEqual(ret, 0, msg="shell should return 0 on eof")
        except TimeoutExpired:
            self.assertTrue(False, msg="shell should terminate on eof")

    def test_simple(self):
        """
        Simply runs date and checks if it works. A simple command with one program, one argument.
        """
        year = str(datetime.now().year)
        self.assertIn(year, self.lsh.run_see_cmd("date"))

    def test_simple2(self):
        """
        A simple command with one program, but this time with two arguments.
        """
        self.assertIn("teststring", self.lsh.run_see_cmd("echo teststring"))

    def test_output_redirect(self):
        """
        Redirecting the output of a command to a file.
        """
        with TempDir() as cwd:
            out = cwd + "/out.txt"
            self.lsh.sendline(f"echo hello > {out}")
            with open(out, "r") as f:
                self.assertIn(
                    "hello",
                    f.readlines()[0],
                    msg="echo was not successfully redirected to output file",
                )

    def test_input_redirect(self):
        """
        Redirecting the input of a command from a file.
        """
        with TempDir() as cwd:
            infile = cwd + "/in.txt"
            with open(infile, "w") as f:
                f.write("input text\n")
            self.assertIn(
                "input text",
                self.lsh.run_see_cmd(f"cat < {infile}"),
                msg="cat didn't read from input file",
            )

    def test_in_out_redirect(self):
        """
        Redirecting to/from files into and out from a command.
        """
        with TempDir() as cwd:
            infile = cwd + "/in.txt"
            outfile = cwd + "/out.txt"
            with open(infile, "w") as f:
                f.write("input\n")
            self.lsh.sendline(f"cat < {infile} > {outfile}")
            with open(outfile, "r") as f:
                self.assertIn(
                    "input",
                    f.readlines()[0],
                    msg="cat didn't write from in.txt to out.txt",
                )

    def test_consecutive_fg(self):
        """
        Test two consecutive foreground commands, and ensure no zombies and correct output.
        """
        out1 = self.lsh.run_see_cmd("echo command1")
        out2 = self.lsh.run_see_cmd("echo command2")

        self.assertIn(
            "command1",
            out1,
            msg="expected to see output from first command",
        )

        self.assertNotIn(
            "command1",
            out2,
            msg="expected to not see output from first command in second command",
        )

        self.assertIn(
            "command2",
            out2,
            msg="expected to see output from second command",
        )

        self.assertFalse(self.lsh.has_zombies(), msg="should not have zombies")

    def test_pipeline(self):
        """
        Test simple pipeline.
        """
        out = self.lsh.run_see_cmd("echo smetsysgnitarepo | rev")

        self.assertIn(
            "operatingsystems",
            out,
            msg="rev command in pipeline didn't work",
        )

        self.assertFalse(self.lsh.has_zombies(), msg="shouldn't have zombies")

    def test_pipeline_concurrency(self):
        """
        All processes in a pipeline should run concurrently.
        """
        self.lsh.sendline("sleep 30 | sleep 30 | sleep 30 | sleep 30")

        self.assertEqual(
            4,
            len(self.lsh.children()),
            msg="All processes in a pipeline should run concurrently; they should not wait for others to finish before starting",
        )

        self.lsh.ctrl_c(wait_after=1)
        self.assertEqual(
            0,
            len(self.lsh.children()),
            msg="All processes in foreground pipeline should respond to Ctrl-C",
        )

        self.assertFalse(self.lsh.has_zombies(), msg="Shouldn't have zombie processes")

    def test_cd(self):
        """
        cd command to change directory
        """
        with TempDir() as tmp:
            abs_tmp = Path(tmp).resolve()
            self.lsh.sendline(f"cd {abs_tmp}")
            self.assertEqual(
                abs_tmp,
                Path(self.lsh.proc().cwd()).resolve(),
                msg="`cd` seemingly didn't change the directory",
            )

    def test_ctrl_c(self):
        """
        Test behaviour of Ctrl-C with just a simple foreground process.
        """
        self.lsh.sendline("sleep 100")
        self.lsh.ctrl_c()
        self.assertEqual(
            0,
            len(self.lsh.children()),
            msg="Ctrl-C didn't terminate foreground child.",
        )

    def test_fg_bg(self):
        """
        Test running a background process at the same time as a foreground one.
        """
        self.lsh.sendline("sleep 3 &")

        self.assertEqual(
            len(self.lsh.children()),
            1,
            msg="it seems no background process was created",
        )

        self.assertEqual(
            ["sleep", "3"],
            self.lsh.children()[0].cmdline(),
            msg="the created background process was not running sleep 3",
        )

        self.lsh.sendline("echo foreground", wait_after=0)
        sleep(3)
        self.assertFalse(self.lsh.has_zombies(), msg="should not have zombies")

    def test_ctrl_c_fg_bg(self):
        """
        Check that Ctrl-C will kill a foreground process, but not a background one.
        """
        self.lsh.sendline("sleep 100 &")
        self.assertEqual(
            1,
            len(self.lsh.children()),
            msg="Not exactly 1 child after executing background command 'cat &'",
        )
        bg_pid = self.lsh.children()[0].pid

        self.lsh.sendline("sleep 100")
        self.assertEqual(
            2,
            len(self.lsh.children()),
            msg="Expected two children, one foreground and one background",
        )

        self.lsh.ctrl_c()
        self.assertEqual(
            1, len(self.lsh.children()), msg="Expected one child after Ctrl-C"
        )

        self.assertEqual(
            bg_pid,
            self.lsh.children()[0].pid,
            msg="Seems the terminated process was the background process",
        )

    def test_bg_sigint(self):
        """
        While a background process should not terminate on Ctrl-C, it should terminate if it directly receives a SIGINT
        """
        self.lsh.sendline("sleep 100 &")
        self.assertEqual(
            1, len(self.lsh.children()), msg="Background process isn't running"
        )
        bg = self.lsh.children()[0]
        os.kill(bg.pid, SIGINT)
        print(f"Killed {bg.pid}")
        sleep(0.5)
        print(f"Children: {self.lsh.children()}")
        self.assertEqual(
            0,
            len(self.lsh.children()),
            msg="Background process *should* terminate from SIGINT",
        )


if __name__ == "__main__":
    unittest.main(
        testRunner=HTMLTestRunner(
            title="Operating Systems Lab 1",
            description=f"Unit tests for lsh built from {LSH_CODE}",
            report_name="test-lsh",
            tested_by=os.getlogin(),
            open_in_browser=True,
        )
    )
