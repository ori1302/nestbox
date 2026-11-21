"""Shared helpers for the Python tests: named checks with a summary, the build directory, polling, and simulators
started headless on a free port with HOME in a temporary folder.

The build directory is `--build DIR`, else $NESTBOX_BUILD_DIR, else build/ in the repository."""
import argparse
import os
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
failures = []


def check(ok, what):
    """Records and prints one check; returns `ok`."""
    print(('  ok   ' if ok else '  FAIL ') + what)
    if not ok:
        failures.append(what)
    return ok


def finish():
    """Prints the summary and exits with 1 if any check failed."""
    print(f'\nFAILED: {len(failures)}' if failures else '\nall passed')
    sys.exit(1 if failures else 0)


def build_dir():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--build')
    args, _ = parser.parse_known_args()
    return os.path.abspath(args.build or os.environ.get('NESTBOX_BUILD_DIR') or os.path.join(ROOT, 'build'))


def display_args():
    """The simulator's --display for $NESTBOX_DISPLAY, if set."""
    display = os.environ.get('NESTBOX_DISPLAY')
    return ['--display', display] if display else []


def free_port():
    with socket.socket() as s:
        s.bind(('', 0))
        return s.getsockname()[1]


def wait_for(predicate, timeout, interval=0.05):
    """Calls `predicate` until it returns something truthy or `timeout` seconds pass; returns its last result."""
    end = time.time() + timeout
    while True:
        result = predicate()
        if result or time.time() >= end:
            return result
        time.sleep(interval)


def port_open(port):
    try:
        with socket.create_connection(('127.0.0.1', port), timeout=0.2):
            return True
    except OSError:
        return False


class Simulator:
    """A headless nestbox-sim with its own port and a temporary HOME, removed by stop()."""

    def __init__(self, build, name, args=(), speed=None, pets=None):
        self.port = free_port()
        self._home = tempfile.TemporaryDirectory(prefix='nestbox-test-home-')
        self.home = self._home.name
        cmd = [os.path.join(build, 'nestbox-sim'), '--headless', '--port', str(self.port), '--name', name,
               '--pets', pets or os.path.join(build, 'test-pets')]
        if speed is not None:
            cmd += ['--speed', str(speed)]
        self.process = subprocess.Popen(cmd + list(args) + display_args(), stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True, env=self.env())
        if not wait_for(lambda: port_open(self.port) or self.process.poll() is not None, 10):
            print(f'the simulator did not open port {self.port}')

    def env(self, **extra):
        """The environment for anything run against this simulator: HOME is the temporary one."""
        return {**os.environ, 'HOME': self.home, **extra}

    def stop(self):
        """Stops the simulator, removes its HOME, and returns its log."""
        exited = self.process.poll()
        if exited is not None:
            print(f'\nthe simulator had already exited with code {exited}' + (' (a signal)' if exited < 0 else ''))
        self.process.terminate()
        log = self.process.communicate(timeout=5)[0]
        self._home.cleanup()
        return log
