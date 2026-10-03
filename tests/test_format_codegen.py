"""Exercise the generator as a build tool, without changing source fixtures."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def check_output(executable):
    result = subprocess.run([executable, "--print"], check=True,
                            stdout=subprocess.PIPE, timeout=10)
    assert result.stdout == "中文🙂 1.25\n".encode("utf-8"), result.stdout
    if os.name != "nt":
        reader, writer = os.pipe()
        os.close(reader)
        try:
            result = subprocess.run([executable, "--print"], stdout=writer,
                                    timeout=10, check=False)
            assert result.returncode == 1, result.returncode
        finally:
            os.close(writer)


def check_generator(arguments):
    root = Path(arguments.directory)
    root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="format space ", dir=root) as tmp:
        directory = Path(tmp)
        source = directory / "input.c"
        output = directory / "generated.c"
        header = directory / "value.h"
        config = Path(arguments.config).resolve()
        command = [sys.executable, arguments.script, "--input", str(source),
                   "--output", str(output), "--clang", arguments.clang,
                   "--config", str(config)]

        def write(path, content):
            path.write_text(content, encoding="utf-8", newline="\n")

        def generate(ok=True, fragment=""):
            before = hashlib.sha256(source.read_bytes()).digest()
            result = subprocess.run(command, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, timeout=120,
                                    check=False)
            diagnostic = result.stderr.decode("utf-8", errors="replace")
            assert (result.returncode == 0) == ok, diagnostic
            assert fragment in diagnostic, diagnostic
            assert hashlib.sha256(source.read_bytes()).digest() == before

        prefix = '#include <hx_format_syntax.h>\n'
        write(header, "typedef double amount;\n")
        write(source, prefix + '#include "value.h"\n'
              'int main(void) { amount n = 1; '
              'return HX_PRINTLN("{:.1f}", n); }\n')
        generate()
        stamp = output.stat().st_mtime_ns
        generate()
        assert output.stat().st_mtime_ns == stamp, "cache rewrote output"
        write(header, "typedef int amount;\n")
        generate(False, "invalid for integer")
        assert output.stat().st_mtime_ns == stamp, "failure replaced output"
        write(header, "typedef float amount;\n")
        generate()
        assert "hx_fmt_f32" in output.read_text(encoding="utf-8")

        failures = [
            ('HX_PRINTLN("{} {}", 1)', "missing argument"),
            ('HX_PRINTLN("{:d}", "text")', "invalid for text"),
            ('HX_FPRINTLN("{unknown.member}")', "unknown"),
            ('HX_PRINTLN("{0:.2f}", 42)', "invalid for integer"),
            ('HX_PRINTLN("{:=4}", "text")', "invalid for text"),
            ('HX_PRINTLN("{:+}", (_Bool)1)', "invalid for text"),
        ]
        for call, diagnostic in failures:
            write(source, prefix + 'int main(void) { return ' + call + '; }')
            generate(False, diagnostic)
        write(source, prefix + 'int main(void) { '
              'return HX_PRINTLN("literal braces {{}}"); }')
        generate()
        write(source, prefix + 'int main(void) { '
              'int hx_format_generated_0 = 1; return 0; }')
        generate(False, "reserved")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True)
    parser.add_argument("--script")
    parser.add_argument("--config")
    parser.add_argument("--clang")
    parser.add_argument("--directory")
    arguments = parser.parse_args()
    check_output(arguments.executable)
    if arguments.script:
        check_generator(arguments)
    print("UTF-8 output, source retention and generator failures passed")


if __name__ == "__main__":
    main()
