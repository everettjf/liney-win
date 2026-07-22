# Windows client certification setup

Liney's hosted CI covers Windows Server 2022/2025. Release certification also
requires real Windows 10 and Windows 11 clients because WSL, desktop power
transitions, multi-monitor topology and client GPU drivers are not equivalent on
Windows Server.

## Security boundary

This is a public repository. GitHub warns that untrusted pull-request code can
compromise a persistent self-hosted runner. These runners must therefore be
disposable test machines with no personal files, browser sessions, SSH keys,
cloud credentials or access to the normal development network.

The certification workflow is deliberately `workflow_dispatch` only, requests
read-only repository permissions and uses the `client-certification`
environment. Configure that environment with a required reviewer, and approve
only `main` or a trusted release tag. Never enter a pull-request ref or an
unreviewed commit as `candidate_ref`.

Because the tests open and automate a desktop window, run the runner with
`run.cmd` in a logged-in interactive test account. Do not install it as a
Windows service: services run in session 0 and cannot validate the real desktop,
DPI or monitor topology.

## Required machines

| Runner label | Machine |
|---|---|
| `liney-windows-10` | Windows 10 22H2 x64, build 19045 |
| `liney-windows-11` | Current Windows 11 x64, two active monitors with different DPI settings |

Both machines require:

- a clean, disposable local test account;
- Git for Windows and GitHub CLI;
- Visual Studio Build Tools with Desktop development with C++ and a Windows SDK;
- `winget` and NSIS;
- WSL with a distro that supports `sh`;
- current display drivers and automatic Windows Update;
- enough free space for the MSVC, Zig and Ghostty builds.

## Register each runner

1. Open repository **Settings → Actions → Runners → New self-hosted runner**.
2. Select Windows x64 and follow the generated download instructions. The
   registration token is time-limited; never commit or paste it into an issue.
3. Add the corresponding custom label to the generated `config.cmd` command,
   for example `--labels liney-windows-10`.
4. Start `run.cmd` from the interactive test desktop and confirm the runner is
   shown as **Idle** in repository settings.
5. Repeat on the other client with `--labels liney-windows-11`.

GitHub's current setup and security guidance is maintained in
[Adding self-hosted runners](https://docs.github.com/en/actions/how-tos/manage-runners/self-hosted-runners/add-runners)
and [Secure use reference](https://docs.github.com/en/actions/reference/security/secure-use).

## Run and retain evidence

Open **Actions → Windows Client Certification → Run workflow**, select trusted
`main` (or a candidate tag) and the preceding release tag. After the required
environment approval, both jobs must finish successfully. Download and retain
the `client-Windows10-*` and `client-Windows11-*` JSON artifacts with the release
record.

The workflow rejects the wrong client build, missing WSL, a single-monitor
Windows 11 machine, failed SSH reconnect lifecycle, failed package upgrade or
uninstall, GPU/device-loss failure, DPI failure, or suspend/resume message
failure. In addition, perform one physical sleep/resume cycle on each candidate
while Liney has an active local shell and WSL tab; confirm both tabs accept new
commands after resume and record the result in the release checklist.
