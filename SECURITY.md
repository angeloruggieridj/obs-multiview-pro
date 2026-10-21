# Security

Please report security problems privately through GitHub: **Security → Report a vulnerability** on this repository. Do not open a public issue for them.

The plugin reads and writes one small JSON settings file in the OBS plugin configuration folder and talks to OBS through its public API.

Its only network connection is the update check: one HTTPS GET to `https://api.github.com/repos/angeloruggieridj/obs-multiview-pro/releases`, automatically at most once a day (never while streaming or recording) and when you press *Check for updates now*. It uses libcurl (the one bundled with OBS on Windows, the system libcurl on macOS and Linux). Certificates are verified (trusted chain and host name); certificate revocation lookups are deliberately off, because with Windows' TLS library they run synchronously inside OBS's user interface thread and could freeze OBS for many seconds on a network that blocks them. The answer is only a public release list, and the only page ever opened is an allow-listed page of this repository.

The request carries exactly `Accept: application/vnd.github+json`, `Accept-Language: en`, `User-Agent: obs-multiview-pro/<version>` and `X-GitHub-Api-Version: 2022-11-28`; no cookies, credentials or system details. GitHub sees your IP address, as with any connection. The answer is size-limited (1 MB) and time-limited (10 s to connect, 10 s below 1 byte/s, 15 s in total); redirects are only followed to `https://api.github.com` or `https://github.com`. The answer is parsed as data: release notes are shown as text, every image form is removed and no image, file or other resource they mention is ever loaded; only links to this repository's GitHub pages are opened, in your browser. The plugin never downloads or runs anything. Turn the automatic check off in *Settings → Updates*.

For testing, the environment variable `OBS_MULTIVIEW_PRO_UPDATE_URL` can point the check at a server on the local machine (127.0.0.1, localhost or [::1] only); any other value is ignored.
