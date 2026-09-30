"""Fetch pinned, unmodified parser sources; never fetch during CMake."""
import argparse
import hashlib
import io
from pathlib import Path
import tarfile
import urllib.request

PACKAGES = {
    "cjson": ("DaveGamble/cJSON", "v1.7.19", "cJSON-1.7.19",
              "7fa616e3046edfa7a28a32d5f9eacfd23"
              "f92900fe1f8ccd988c1662f30454562"),
    "libyaml": ("yaml/libyaml", "0.2.5", "libyaml-0.2.5",
                "fa240dbf262be053f3898006d502d5149"
                "36c818e422afdcf33921c63bed9bf2e"),
    "minixml": ("michaelrsweet/mxml", "v4.0.6", "mxml-4.0.6",
                "f5bb4ceb7506880e7ba389a439da7ab54"
                "565530de595cf661838ba551de35c47"),
    "inih": ("benhoyt/inih", "r62", "inih-r62",
             "9c15fa751bb8093d042dae1b9f125eb451"
             "98c32c6704cd5481ccde460d4f8151"),
    "tomlc": ("cktan/tomlc17", "R260821", "tomlc17-R260821",
              "c4958fe2664d596ba1cc22589f56f9004"
              "29fac89ad0738c3468c6e1021e92c1a"),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("packages", nargs="*", choices=tuple(PACKAGES))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1] / ".deps"
    root.mkdir(exist_ok=True)
    for name in args.packages or PACKAGES:
        repository, tag, directory, checksum = PACKAGES[name]
        destination = root / directory
        if destination.exists():
            print(f"Existing source retained: {destination}")
            continue
        archive_path = root / f"{name}-{tag}.tar.gz"
        if archive_path.exists():
            data = archive_path.read_bytes()
        else:
            url = (f"https://codeload.github.com/{repository}/tar.gz/"
                   f"refs/tags/{tag}")
            data = urllib.request.urlopen(url, timeout=60).read()
        if hashlib.sha256(data).hexdigest() != checksum:
            raise RuntimeError(f"Archive checksum mismatch: {name}")
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
            for member in archive.getmembers():
                if member.name.split("/")[0] != directory:
                    raise RuntimeError("Unexpected archive root")
            archive.extractall(root, filter="data")
        print(f"Source ready: {destination}")


if __name__ == "__main__":
    main()

