#!/usr/bin/env python3
"""
Checks that every native method the JNI bridge exports is declared in Java with
exactly the same name and arity, and that nothing is declared on one side only.

A mismatch is a runtime crash on the first call, and it is the most likely
mistake in a project with this shape: the bridge is C++, the declarations are
Java, and nothing at compile time connects them.  The desktop tests cannot catch
it because they never go through JNI.

    python3 tools/check_jni.py
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BRIDGE = os.path.join(ROOT, "app/src/main/cpp/platform/jni_bridge.cpp")
JAVA = os.path.join(ROOT, "app/src/main/java/com/pulsepoint/app/PulsePoint.java")
PACKAGE = "com.pulsepoint.app"

NATIVE_RE = re.compile(
    r"Java_%s_(\w+?)_(native\w+)\(([^)]*)\)\s*\{" % PACKAGE.replace(".", "_")
)
JAVA_NATIVE_RE = re.compile(
    r"static\s+native\s+([\w.]+(?:<[^>]*>)?(?:\[\])?)\s+(native\w+)\s*\(([^)]*)\)\s*;"
)

# JNI's C types, and what they mean on the Java side.
PRIMITIVES = {
    "jboolean": "boolean",
    "jbyte": "byte",
    "jchar": "char",
    "jshort": "short",
    "jint": "int",
    "jlong": "long",
    "jfloat": "float",
    "jdouble": "double",
}
# JNI injects the environment and the receiver; Java never mentions them.



def split_param(text):
    """Splits 'const jint x' into ('jint', 'x').

    Returns the base type without pointer or reference markers, which is what
    both the implicit-parameter filter and the type comparison want.
    """
    text = text.strip()
    if " " in text:
        head, tail = text.rsplit(" ", 1)
        if re.fullmatch(r"[\w*&]+", tail):
            text = head.strip()
    return text.rstrip("*&").strip(), ""


def cpp_param_types(params):
    out = []
    for p in params.split(","):
        p = p.strip()
        if not p:
            continue
        t, _ = split_param(p)
        # JNI injects these; Java never mentions them.
        if t in ("JNIEnv", "jclass"):
            continue
        out.append(t)
    return out


def java_param_types(params):
    out = []
    for p in params.split(","):
        p = p.strip()
        if not p:
            continue
        t = p.rsplit(" ", 1)[0].strip()
        out.append(t)
    return out


def types_agree(cpp, java):
    """A C++ jobject accepts any reference type; primitives must match exactly."""
    if cpp == "jobject":
        return not java in PRIMITIVES.values()
    if cpp == "jstring":
        return java == "String"
    return PRIMITIVES.get(cpp) == java


def main():
    with open(BRIDGE) as fh:
        bridge_src = fh.read()
    with open(JAVA) as fh:
        java_src = fh.read()

    native = {}
    for m in NATIVE_RE.finditer(bridge_src):
        cls, method, params = m.group(1), m.group(2), m.group(3)
        native[(cls, method)] = cpp_param_types(params)

    java = {}
    for m in JAVA_NATIVE_RE.finditer(java_src):
        ret, method, params = m.group(1), m.group(2), m.group(3)
        java[method] = (ret, java_param_types(params))

    if not native:
        print("no native methods found in the bridge -- the pattern is stale")
        return 1
    if not java:
        print("no native declarations found in Java -- the pattern is stale")
        return 1

    problems = []
    for (cls, method), sig in sorted(native.items()):
        if method not in java:
            problems.append("bridge exports %s.%s; Java does not declare it" % (cls, method))
            continue
        _, java_sig = java[method]
        if len(sig) != len(java_sig):
            problems.append(
                "%s: C++ takes %d args (%s), Java declares %d (%s)"
                % (method, len(sig), ", ".join(sig), len(java_sig), ", ".join(java_sig))
            )
            continue
        for c, j in zip(sig, java_sig):
            if not types_agree(c, j):
                problems.append("%s: C++ %s does not match Java %s" % (method, c, j))

    for method in sorted(java):
        if not any(method == m for _, m in native):
            problems.append("Java declares native %s; the bridge does not export it" % method)

    print("bridge exports %d native methods, Java declares %d" % (len(native), len(java)))
    for p in problems:
        print("  MISMATCH %s" % p)
    if problems:
        return 1
    print("every native method lines up on both sides")
    return 0


if __name__ == "__main__":
    sys.exit(main())
