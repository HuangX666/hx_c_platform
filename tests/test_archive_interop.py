"""Exercise the archive program with independent formats and hostile paths."""

import argparse
import concurrent.futures
import gzip
import io
import lzma
import os
from pathlib import Path
import stat
import subprocess
import tarfile
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--example", required=True, type=Path)
    parser.add_argument("--vendor", type=Path)
    parser.add_argument("--adapter", type=Path)
    parser.add_argument("--cli", type=Path)
    args = parser.parse_args()
    environment = os.environ.copy()
    for key in ("HX_ARCHIVE_PASSWORD", "HX_7ZIP_LIBRARY", "HX_7ZIP_ADAPTER"):
        environment.pop(key, None)

    def run(*arguments, success=True, env=None):
        result = subprocess.run(
            [str(args.example), *map(str, arguments)],
            env=env or environment, capture_output=True, timeout=30)
        sanitizer = any(marker in result.stderr for marker in (
            b"ERROR: AddressSanitizer", b"ERROR: LeakSanitizer",
            b"WARNING: ThreadSanitizer", b"runtime error:"))
        if result.returncode != (0 if success else 1) or sanitizer:
            raise AssertionError((arguments, result.returncode,
                                  result.stdout, result.stderr))
        return result

    with tempfile.TemporaryDirectory(prefix="hx-archive-interop-") as name:
        root = Path(name)
        source = root / "设备数据"
        nested = "中文/😀/a/b/c/d/e/f/g/h/样本.json"
        data = b'{"samples":[1,"text",null,{"values":[true,2.5]}]}'
        (source / nested).parent.mkdir(parents=True)
        (source / nested).write_bytes(data)
        (source / "empty").mkdir()

        for suffix in ("zip", "tar", "tar.gz", "tar.xz", "tar.zst", "7z"):
            archive = root / ("snapshot." + suffix)
            run("pack", source, archive)
            destination = root / ("restored-" + suffix)
            run("unpack", archive, destination)
            assert (destination / nested).read_bytes() == data
            assert (destination / "empty").is_dir()
            run("unpack", archive, destination, success=False)
            if suffix == "zip":
                with zipfile.ZipFile(archive) as foreign:
                    assert foreign.read(nested) == data
            elif suffix in ("tar", "tar.gz", "tar.xz"):
                with tarfile.open(archive) as foreign:
                    assert foreign.extractfile(nested).read() == data

        foreign_zip = root / "foreign.zip"
        with zipfile.ZipFile(foreign_zip, "w", zipfile.ZIP_DEFLATED) as writer:
            writer.writestr(nested, data)
        run("extract", foreign_zip, nested, root / "one.json")
        assert (root / "one.json").read_bytes() == data
        run("extract", foreign_zip, "absent", root / "missing", success=False)

        long_name = "/".join(["segment-name"] * 30) + "/file.bin"
        long_archive = root / "long.zip"
        with zipfile.ZipFile(long_archive, "w") as writer:
            writer.writestr(long_name, data)
        run("unpack", long_archive, root / "long")
        long_file = root / "long" / long_name
        if os.name == "nt":
            long_file = Path("\\\\?\\" + str(long_file))
        assert long_file.read_bytes() == data

        for suffix in ("gz", "xz", "zst"):
            plain = root / "zero"
            plain.write_bytes(b"")
            archive = root / ("zero." + suffix)
            run("pack", plain, archive)
            destination = root / ("zero-" + suffix)
            run("unpack", archive, destination)
            assert (destination / "zero").read_bytes() == b""

        multi = root / "multi.gz"
        multi.write_bytes(gzip.compress(b"first") + gzip.compress(b"second"))
        run("unpack", multi, root / "multi")
        assert (root / "multi/multi").read_bytes() == b"firstsecond"

        for suffix, compress in (("gz", gzip.compress), ("xz", lzma.compress)):
            archive = root / ("foreign." + suffix)
            archive.write_bytes(compress(data * 5000))
            run("unpack", archive, root / ("foreign-" + suffix))
            assert (root / ("foreign-" + suffix) / "foreign").read_bytes() == (
                data * 5000)
            broken = bytearray(archive.read_bytes())
            broken[-5] ^= 1
            archive.write_bytes(broken)
            destination = root / ("broken-" + suffix)
            run("unpack", archive, destination, success=False)
            assert not (destination / "foreign").exists()

        tar_bytes = io.BytesIO()
        with tarfile.open(fileobj=tar_bytes, mode="w") as writer:
            info = tarfile.TarInfo("./")
            info.type = tarfile.DIRTYPE
            writer.addfile(info)
            info = tarfile.TarInfo("./payload.bin")
            info.size = len(data)
            writer.addfile(info, io.BytesIO(data))
        archive = root / "foreign.tar"
        archive.write_bytes(tar_bytes.getvalue())
        run("unpack", archive, root / "foreign-tar")
        assert (root / "foreign-tar/payload.bin").read_bytes() == data

        # Validate the compression trailer even after logical TAR EOF.
        damaged = bytearray(gzip.compress(
            tar_bytes.getvalue() + b"\0" * 200000))
        damaged[-8] ^= 1
        archive = root / "bad-tail.tar.gz"
        archive.write_bytes(damaged)
        run("unpack", archive, root / "bad-tail", success=False)
        assert not (root / "bad-tail/payload.bin").exists()

        for index, path in enumerate(("../escape", "/absolute", "C:/drive",
                                      "..\\escape", "a/../escape", "NUL.txt",
                                      "a:stream", "trailing. ",
                                      "/".join(["deep"] * 65) + "/file")):
            archive = root / f"unsafe-{index}.zip"
            with zipfile.ZipFile(archive, "w") as writer:
                writer.writestr(path, b"must not escape")
            run("unpack", archive, root / f"unsafe-{index}", success=False)
        assert not (root / "escape").exists()

        archive = root / "crc.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_STORED) as writer:
            writer.writestr("payload", b"unique payload marker")
        damaged = archive.read_bytes().replace(
            b"unique payload marker", b"broken payload marker")
        archive.write_bytes(damaged)
        run("unpack", archive, root / "crc", success=False)
        assert not (root / "crc/payload").exists()

        archive = root / "links.tar"
        with tarfile.open(archive, "w") as writer:
            info = tarfile.TarInfo("escape")
            info.type = tarfile.SYMTYPE
            info.linkname = "../outside"
            writer.addfile(info)
        run("unpack", archive, root / "links", success=False)
        assert not (root / "links/escape").is_symlink()

        archive = root / "links.zip"
        with zipfile.ZipFile(archive, "w") as writer:
            info = zipfile.ZipInfo("link")
            info.create_system = 3
            info.external_attr = (stat.S_IFLNK | 0o777) << 16
            writer.writestr(info, "../outside")
        run("unpack", archive, root / "zip-links", success=False)

        outside = root / "outside"
        outside.mkdir()
        existing = root / "existing"
        existing.mkdir()
        try:
            (existing / "中文").symlink_to(outside, target_is_directory=True)
        except OSError as error:
            if os.name != "nt":
                raise
            print(f"Native link creation unavailable: {error}")
        else:
            run("unpack", foreign_zip, existing, success=False)
            assert not list(outside.iterdir())

        def concurrent_job(index):
            archive = root / f"concurrent-{index}.tar.xz"
            run("pack", source, archive)
            run("unpack", archive, root / f"concurrent-{index}")

        with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
            list(pool.map(concurrent_job, range(6)))

        encrypted_zip = root / "private.zip"
        zip_env = dict(environment, HX_ARCHIVE_PASSWORD="test-only-密码🔑")
        run("pack", source, encrypted_zip, env=zip_env)
        run("unpack", encrypted_zip, root / "private-zip", env=zip_env)
        assert (root / "private-zip" / nested).read_bytes() == data
        if args.cli:
            # Windows 7-Zip converts ZIP passwords through CP_ACP. Test
            # shared ASCII password semantics independently of that policy.
            portable_zip = root / "portable-private.zip"
            portable_env = dict(environment, HX_ARCHIVE_PASSWORD="test-only")
            run("pack", source, portable_zip, env=portable_env)
            subprocess.run([str(args.cli), "t", str(portable_zip),
                            "-ptest-only"], check=True,
                           capture_output=True, timeout=30)

        if args.vendor and args.adapter:
            secure = dict(environment, HX_ARCHIVE_PASSWORD="test-only-密码🔑",
                          HX_7ZIP_LIBRARY=str(args.vendor),
                          HX_7ZIP_ADAPTER=str(args.adapter))
            archive = root / "private.7z"
            run("pack", source, archive, env=secure)
            run("list", archive, env=secure)
            run("unpack", archive, root / "private", env=secure)
            assert (root / "private" / nested).read_bytes() == data
            wrong = dict(secure, HX_ARCHIVE_PASSWORD="incorrect")
            run("list", archive, env=wrong, success=False)
            # Dispatch from magic, regardless of the filename extension.
            renamed = root / "private.blob"
            archive.rename(renamed)
            run("extract", renamed, nested, root / "private.json", env=secure)
            assert (root / "private.json").read_bytes() == data
            if args.cli:
                subprocess.run([str(args.cli), "t", str(renamed),
                                "-ptest-only-密码🔑"], check=True,
                               capture_output=True, timeout=30)

    print("Foreign formats, integrity, UTF-8, bounds and paths passed")


if __name__ == "__main__":
    main()
