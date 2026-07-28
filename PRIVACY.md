# Liney Terminal Privacy Policy

Effective date: July 28, 2026

Liney Terminal is a native Windows terminal workspace. It does not include
advertising, analytics, tracking, or developer-operated telemetry.

## Data stored on your device

Liney stores its configuration, workspace projects, recent projects, session
layout, command history, and crash-recovery information locally under your
Windows user profile. This information is not sent to the Liney developers.

Diagnostic bundles are created only when you explicitly request one. They
remain on your device until you choose to share or delete them and may contain
terminal output, file paths, environment information, or other sensitive data.
You should inspect a diagnostic bundle before sharing it.

## Network connections

Liney opens network connections only for features you choose to use:

- SSH, WSL, shells, agents, Git tools, and other commands run with the
  arguments and destinations you configure.
- Optional AI assistance sends the selected command context to the provider
  and endpoint you configure only when you explicitly invoke the feature.
  Provider processing is governed by that provider's privacy policy.
- GitHub-distributed builds may contact GitHub to check for signed or
  checksum-verified updates. Microsoft Store builds receive application
  updates exclusively through Microsoft Store and do not use Liney's GitHub
  self-updater.
- Links such as support, release, or issue pages open in your default browser
  only when you select them.

Liney does not operate an account service and does not sell personal
information.

## Clipboard and terminal content

Terminal programs can request clipboard access through standard terminal
control sequences. Liney applies the clipboard policy configured by the user.
Terminal content remains local unless you explicitly copy, export, share, or
send it through a configured command, remote session, or AI provider.

## Data retention and deletion

You control locally stored Liney data. You can remove it by deleting the Liney
configuration and diagnostics folders under your Windows user profile. Remote
services used through SSH, Git, agents, or AI providers have their own
retention policies.

## Contact

For privacy questions or requests, open an issue at:

https://github.com/everettjf/liney-win/issues

