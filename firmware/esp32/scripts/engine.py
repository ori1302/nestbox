# Compiles the portable engine (../../engine) into the firmware unchanged, with the firmware's own flags.
import os

Import("env")  # noqa: F821 (PlatformIO)

engine = os.path.normpath(os.path.join(env.subst("$PROJECT_DIR"), "..", "..", "engine"))  # noqa: F821
env.Append(CPPPATH=[engine])  # noqa: F821
env.BuildSources(os.path.join("$BUILD_DIR", "engine"), engine)  # noqa: F821
