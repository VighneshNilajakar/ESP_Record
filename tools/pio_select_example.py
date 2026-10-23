"""Point PlatformIO at the sketch named by an environment's `custom_example`.

PlatformIO's `src_dir` is a project-level option: inside an `[env:...]` section it
is ignored, which one platformio.ini cannot advertise away when it wants to build
several sketches. `PROJECT_SRC_DIR` is substituted when the source list is built,
after extra scripts have run, so replacing it here is the supported hook.

An environment without `custom_example` keeps the default `src/` behaviour.
"""

import os

Import("env")

example = env.GetProjectOption("custom_example", "")
if example:
    env.Replace(PROJECT_SRC_DIR=os.path.join(env.subst("$PROJECT_DIR"), example))
