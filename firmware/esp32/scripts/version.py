# Defines NESTBOX_VERSION ("x.y.z") and NESTBOX_USB_REVISION ("x.y", the USB drive's 4-character SCSI revision)
# from the project version in ../../CMakeLists.txt, the one place the version is set.
import os
import sys

Import("env")  # noqa: F821 (PlatformIO)

sys.path.insert(0, os.path.join(env.subst("$PROJECT_DIR"), "scripts"))  # noqa: F821
import project  # noqa: E402

major, minor, patch = project.version()
env.Append(CPPDEFINES=[  # noqa: F821
    ("NESTBOX_VERSION", env.StringifyMacro("%s.%s.%s" % (major, minor, patch))),  # noqa: F821
    ("NESTBOX_USB_REVISION", env.StringifyMacro("%s.%s" % (major, minor))),  # noqa: F821
])
