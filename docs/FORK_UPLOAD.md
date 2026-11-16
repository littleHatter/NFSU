# Applying this source package to an MGSM fork

1. Fork https://github.com/LeithSilver/mgsm using GitHub.
2. Clone your fork or use GitHub's file-upload interface.
3. Extract this ZIP. Copy its contents into the repository root, merging folders.
   README.md and .gitignore replace the root versions; the original README is
   also included as README_MGSM_UPSTREAM.md. Existing platform code is retained.
4. Commit the added/updated files and build with `make -C nfs_undercover -j2`.

The package contains the complete NFS port sources and build artwork, not just
a patch. It intentionally omits game archives, XE3U, saves, SDK files, generated
executables, debug symbols, test logs and .git history. Do not upload the separate
complete-install ZIP's game directory as part of this source commit.

Upstream revision used for layout and provenance:
4e6d91ceadfb898322f535f82f24a394197d892b.

When extracting with an archive program, include .gitignore. If your tool omits
hidden files, add that file explicitly. Keep the upstream LICENSE and notices.
