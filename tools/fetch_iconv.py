"""Fetch the pinned, unmodified GNU libiconv release into the local cache."""
import hashlib
import io
from pathlib import Path
import tarfile
import urllib.request

VERSION = "1.18"
SHA256 = "3b08f5f4f9b4eb82f151a7040bfd6fe6c6fb922efe4b1659c66ea933276965e8"
ROOT = Path(__file__).resolve().parents[1] / ".deps"
DESTINATION = ROOT / f"libiconv-{VERSION}"
URL = f"https://ftp.gnu.org/pub/gnu/libiconv/libiconv-{VERSION}.tar.gz"


def main():
    if DESTINATION.exists():
        print(f"Existing source retained: {DESTINATION}")
        return
    data = urllib.request.urlopen(URL, timeout=60).read()
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise RuntimeError("Archive checksum mismatch")
    ROOT.mkdir(exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        archive.extractall(ROOT, filter="data")
    print(f"Source ready: {DESTINATION}")


if __name__ == "__main__":
    main()
