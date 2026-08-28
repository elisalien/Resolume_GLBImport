Drop the archive for your platform, unzip it, and run the installer inside.

**Windows** — `installer.bat`. It checks for the Visual C++ runtime the plugin
needs (without it Resolume skips the plugin silently) and finds your Documents
folder even when it is redirected to OneDrive.

**macOS** — `installer.command`, right-click → Open the first time. Universal
binary, Apple Silicon and Intel. The installer clears the Gatekeeper quarantine
that would otherwise stop Resolume from loading the bundle.

Close Resolume before installing: plugins are only scanned at startup.

Requires Resolume Arena or Avenue 7.4.1 or newer, and a GPU with OpenGL 4.1.

See the [changelog](../blob/main/CHANGELOG.md) for what is in this version, and
the [README](../blob/main/README.md) for the full parameter reference.
