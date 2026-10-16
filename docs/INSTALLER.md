# One-command installation

The source installer targets apt-based desktop systems with Qt 6.4+: Linux Mint 22+, Ubuntu 24.04+ and Debian 12+. It installs distribution development dependencies using sudo apt-get, downloads the v0.1.0 source tag, builds in a temporary directory as the desktop user, and installs to ~/.local. Temporary build files are removed on completion or failure. The entire installer must not run as root.

`--prefix /absolute/path`, `--no-deps` and `--source /checkout` support existing development environments. The source override avoids any download. Missing tools, invalid arguments, unsupported Qt packages, failed downloads/builds/installs abort rather than claiming success.

Verification on Debian 13: `bash -n`, help output, rejection of unknown options and relative prefixes, and a complete Release build/install using the existing Qt SDK with `--no-deps --source /workspace --prefix /workspace/build/install-check` all passed. The dependency-install branch was not executed because this managed environment prevents system package installation. Mint/Ubuntu compatibility still needs clean-machine validation.

## GitHub distribution

Repository: https://github.com/klcenka9/frameforge . The v0.1.0 tag supplies the source installer and source archive. GitHub Releases carry the Debian 13 x86_64 package and complete project ZIP. The source installer is the recommended path for other supported distributions; binary compatibility with Mint is not implied.
