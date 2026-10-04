"""Process lifetime and logs only; each runner owns its business assertions."""

from pathlib import Path
import json
import subprocess
import sys
import tempfile
import time


# Keep captured JSON logs valid UTF-8 on Windows code-page configurations.
for stream in (sys.stdout, sys.stderr):
    if hasattr(stream, "reconfigure"):
        stream.reconfigure(encoding="utf-8")


def json_messages(log, prefix):
    messages = []
    for line in log.splitlines():
        if line.startswith(prefix):
            message = json.loads(line[len(prefix):])
            if not isinstance(message, dict):
                raise RuntimeError("Business payload must be a JSON object")
            messages.append(message)
    return messages


def executable(directory, name):
    suffix = ".exe" if sys.platform == "win32" else ""
    result = Path(directory).resolve() / ("hx_os_" + name + suffix)
    if not result.is_file():
        raise RuntimeError(f"Build the example first: {result}")
    return result


def stop(process):
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3)


def ready(process, log):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        with log.open("rb") as stream:
            line = stream.readline(512)
        if line.endswith(b"\n") and line.startswith(b"Listening: "):
            return line[len(b"Listening: "):].decode("utf-8").strip()
        if process.poll() is not None:
            break
        time.sleep(0.02)
    raise RuntimeError("Server did not publish its listening address")


def run_pair(directory, server_name, client_name, listen, verify,
             server_args=(), client_args=(), client_action=None):
    server = executable(directory, server_name)
    client = executable(directory, client_name) if client_name else None
    process = None
    with tempfile.TemporaryDirectory(prefix="hx-business-") as temporary:
        output = Path(temporary) / "server.out"
        errors = Path(temporary) / "server.err"
        try:
            with output.open("wb") as stdout, errors.open("wb") as stderr:
                process = subprocess.Popen(
                    [str(server), listen, *server_args],
                    stdout=stdout, stderr=stderr,
                )
            actual = ready(process, output)
            print(f"Listening: {actual}", flush=True)
            if client_action:
                client_text = client_action(actual)
            else:
                result = subprocess.run(
                    [str(client), actual, *client_args], capture_output=True,
                    text=True, encoding="utf-8", errors="replace", timeout=25,
                )
                client_text = result.stdout
                print(client_text, end="")
                if result.stderr:
                    print(result.stderr, file=sys.stderr, end="")
                if result.returncode:
                    raise RuntimeError(f"Client exit code: {result.returncode}")
            process.wait(timeout=10)
            if process.returncode:
                raise RuntimeError(f"Server exit code: {process.returncode}")
            verify(output.read_text(encoding="utf-8"), client_text)
        finally:
            stop(process)
            if output.exists():
                print(output.read_text(encoding="utf-8", errors="replace"),
                      end="")
            if errors.exists():
                print(errors.read_text(encoding="utf-8", errors="replace"),
                      file=sys.stderr, end="")


def checked_main(main):
    try:
        main()
    except (OSError, RuntimeError, ValueError,
            subprocess.TimeoutExpired) as err:
        print(f"FAIL: {err}", file=sys.stderr)
        return 1
    return 0
