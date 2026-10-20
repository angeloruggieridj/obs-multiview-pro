# Security

Please report security problems privately through GitHub: **Security → Report a vulnerability** on this repository. Do not open a public issue for them.

The plugin reads and writes one small JSON settings file in the OBS plugin configuration folder and talks to OBS through its public API.

Its only network connection is the update check, made with the libcurl that OBS ships (certificate verification always on): one HTTPS GET to `https://api.github.com/repos/angeloruggieridj/obs-multiview-pro/releases`, automatically at most once a day (never while streaming or recording) and when you press *Check for updates now*. The request carries `User-Agent: obs-multiview-pro/<version>` and the headers the GitHub API asks for; no cookies, credentials or system details. The answer is size-limited (1 MB), time-limited (10 s to connect, 10 s below 1 byte/s, 15 s in total), redirects are only followed to GitHub https addresses, and parsed as data: release notes are shown as text with no remote images, and only links to this repository's GitHub pages are opened, in your browser. The plugin never downloads or runs anything. Turn the automatic check off in *Settings → Updates*.
