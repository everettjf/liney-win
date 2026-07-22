# TODO

## Deferred: Windows code signing and Store distribution

Code signing is intentionally excluded from the current v0.8.x stability goal.
Unsigned GitHub release artifacts remain acceptable until one of the following
distribution paths is ready.

- [ ] Microsoft Store
  - Wait for Microsoft to reactivate the Partner Center developer account.
  - Reserve the `Liney` or `Liney Terminal` product name.
  - Replace the placeholder MSIX package identity with the values assigned by
    Partner Center.
  - Disable GitHub self-update behavior in Store builds and submit a private
    package flight before production release.

- [ ] SignPath Foundation
  - Add a public code signing policy and privacy policy to the repository.
  - Confirm GitHub and SignPath MFA and document committer, reviewer, and
    approver roles.
  - Apply for the free open-source signing program.
  - After approval, integrate SignPath origin-verified signing into the GitHub
    Actions release workflow for `Liney.exe` and `liney-setup.exe`.
  - Verify Authenticode signatures and retain installer upgrade/uninstall smoke
    tests before publishing each release.

