# Security

Report security problems privately: **Security → Report a vulnerability** on this repository. Please do not open a public issue.

## What the plugin does

- Reads and writes one small JSON settings file in the OBS plugin configuration folder.
- Talks to OBS only through its public API.
- Its only network connection is the **update check**.

## The update check

- **When**: automatically at most once a day (never while streaming or recording), or when you press *Check for updates now*. Turn the automatic check off in *Settings → Updates*.
- **What**: one HTTPS GET to `https://api.github.com/repos/angeloruggieridj/obs-multiview-pro/releases`, made with libcurl (the one bundled with OBS on Windows, the system one on macOS and Linux).
- **Sent**: `Accept: application/vnd.github+json`, `Accept-Language: en`, `User-Agent: obs-multiview-pro/<version>`, `X-GitHub-Api-Version: 2022-11-28`. No cookies, credentials or system details. GitHub sees your IP address, as with any connection.
- **TLS**: certificates and host name are verified. Revocation lookups are deliberately off: with Windows' TLS library they run inside OBS's interface thread and could freeze it for many seconds on a network that blocks them.
- **Limits**: answer at most 1 MB; 10 s to connect, 10 s below 1 byte/s, 15 s in total; redirects only to `https://api.github.com` or `https://github.com`.
- **Release notes**: shown as text. Every image form is removed and no image, file or other resource they mention is loaded. Only links to this repository's GitHub pages are opened, in your browser.
- **Never**: the plugin downloads or runs nothing.

## Testing aid

`OBS_MULTIVIEW_PRO_UPDATE_URL` can point the check at a server on the local machine (127.0.0.1, localhost or [::1]); any other value is ignored.
