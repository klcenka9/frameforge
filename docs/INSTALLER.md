# One-command installation

The source installer targets apt-based desktop systems with Qt 6.4+: Linux Mint 22+, Ubuntu 24.04+ and Debian 12+. It installs distribution development dependencies using sudo apt-get, downloads the v0.1.0 source tag, builds in a temporary directory as the desktop user, and installs to ~/.local. Temporary build files are removed on completion or failure. The entire installer must not run as root.

`--prefix /absolute/path`, `--no-deps` and `--source /checkout` support existing development environments. The source override avoids any download. Missing tools, invalid arguments, unsupported Qt packages, failed downloads/builds/installs abort rather than claiming success.

Verification on Debian 13: `bash -n`, help output, rejection of unknown options and relative prefixes, and a complete Release build/install using the existing Qt SDK with `--no-deps --source /workspace --prefix /workspace/build/install-check` all passed. The dependency-install branch was not executed because this managed environment prevents system package installation. Mint/Ubuntu compatibility still needs clean-machine validation.

Publication is pending: the GitHub connection authenticated as klcenka9, but both GraphQL createRepository and REST POST /user/repos returned Resource not accessible by integration (REST HTTP 403). An account owner must create the empty repository before this connection can upload the prepared source, v0.1.0 tag and release assets. README's public install URL becomes usable only after that publication.
