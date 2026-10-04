"""Independent readers and seeded mixed-tree round trips."""
import argparse
import ctypes as ct
import json
import random
import subprocess
import xml.etree.ElementTree as ET
try:
    import tomllib
except ImportError:
    tomllib = None
try:
    import yaml
except ImportError:
    yaml = None


def run(program, *arguments):
    output = subprocess.check_output([program, *arguments], timeout=20)
    return output.decode("utf-8")


def typed_equal(a, b):
    assert type(a) is type(b), (type(a), type(b), a, b)
    if isinstance(a, dict):
        assert a.keys() == b.keys()
        for key in a:
            typed_equal(a[key], b[key])
    elif isinstance(a, list):
        assert len(a) == len(b)
        for left, right in zip(a, b):
            typed_equal(left, right)
    else:
        assert a == b, (a, b)


def examples(arguments):
    expected = {
        "name": "客厅传感器", "enabled": True,
        "samples": [{"unit": "C", "value": 23.5, "channel": 1},
                    {"unit": "%", "value": 60.0, "channel": 2}],
        "range": {"low": -10.0, "high": 80.0},
    }
    typed_equal(expected, json.loads(run(arguments.example, "json")))
    if tomllib:
        typed_equal(expected, tomllib.loads(run(arguments.example, "toml")))
    else:
        print("tomllib unavailable: independent TOML check skipped")
    if yaml:
        typed_equal(expected, yaml.safe_load(run(arguments.example, "yaml")))
    else:
        print("PyYAML unavailable: optional independent YAML check skipped")
    root = ET.fromstring(run(arguments.example, "xml"))
    assert root.tag == "device" and root.findtext("name") == expected["name"]
    assert root.findtext("enabled") == "true"
    assert len(root.findall("samples/item")) == 2
    assert root.findtext("samples/item/value") == "23.5"
    nested = json.loads(run(arguments.nested))
    typed_equal(nested, {"items": [1, "文字", None, {"children": [True, 2.5]}]})


class String(ct.Structure):
    _fields_ = [("data", ct.c_void_p), ("size", ct.c_size_t),
                ("capacity", ct.c_size_t), ("context", ct.c_void_p)]


def mixed_values(library):
    dll = ct.CDLL(library)
    dll.hx_os_config_parse.argtypes = [ct.c_void_p, ct.c_int, ct.c_void_p,
        ct.c_size_t, ct.c_void_p, ct.POINTER(ct.c_void_p), ct.c_void_p]
    dll.hx_os_config_parse.restype = ct.c_int
    dll.hx_os_config_serialize.argtypes = [ct.c_void_p, ct.c_int,
        ct.POINTER(String), ct.c_void_p, ct.c_void_p]
    dll.hx_os_config_serialize.restype = ct.c_int
    dll.hx_os_config_release.argtypes = [ct.c_void_p]
    dll.hx_string_destroy.argtypes = [ct.POINTER(String)]
    rng = random.Random(20261004)
    scalars = [None, True, False, 0, -3, 1.0, -0.0, 2.5,
               9007199254740993, "", "true", "文字🚀", "\0\n\t\"\\",
               "\u0085\u2028\u2029\ufffe\uffff", '{"nested":"still a string"}']

    def value(depth):
        if not depth or rng.randrange(3) == 0:
            return rng.choice(scalars)
        if rng.randrange(2):
            return [value(depth - 1) for _ in range(rng.randrange(5))]
        return {f"key {index} /~\0": value(depth - 1)
                for index in range(rng.randrange(5))}

    cases = [{"items": [1, "文字", None, {"children": [True, 2.5, {}, []]}]}]
    cases += [value(7) for _ in range(100)]
    for original in cases:
        raw = json.dumps(original, ensure_ascii=False,
                         separators=(",", ":")).encode("utf-8")
        document, output = ct.c_void_p(), String()
        assert dll.hx_os_config_parse(None, 0, raw, len(raw), None,
                                     ct.byref(document), None) == 0
        try:
            status = dll.hx_os_config_serialize(
                document, 0, ct.byref(output), None, None)
            assert status == 0
            encoded = ct.string_at(output.data, output.size)
            typed_equal(original, json.loads(encoded))
            if yaml:
                status = dll.hx_os_config_serialize(
                    document, 1, ct.byref(output), None, None)
                assert status == 0
                encoded = ct.string_at(output.data, output.size).decode("utf-8")
                typed_equal(original, yaml.safe_load(encoded))
        finally:
            dll.hx_string_destroy(ct.byref(output))
            dll.hx_os_config_release(document)
    print("101 seeded mixed JSON trees preserved every type; "
          "independent readers passed")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--example", required=True)
    parser.add_argument("--nested", required=True)
    parser.add_argument("--library")
    args = parser.parse_args()
    examples(args)
    if args.library:
        mixed_values(args.library)
    print("serialization interoperability passed")


if __name__ == "__main__":
    main()
