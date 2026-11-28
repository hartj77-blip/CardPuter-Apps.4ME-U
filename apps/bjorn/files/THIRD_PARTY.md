# Third-party code

This app packages **Bjorn** and runs it headless on ZeroOS. The Bjorn code is
not this repository's; it is fetched at build time by `prepare.sh` at a pinned
commit and installed unmodified, apart from two headless changes made by
`prepare.sh`: adding `resources/waveshare_epd/epd_mock.py` (shipped by this app)
and defaulting `epd_type` to it.

| Project | Author | Licence | Used for |
| --- | --- | --- | --- |
| [Bjorn](https://github.com/infinition/Bjorn) | infinition | MIT | The whole application: the recon orchestrator, the actions, and the web dashboard (`webapp.py`, `web/`) |

Pinned commit: `61a4758e5ee3d37d43c10eb690340626139912c0` (see `prepare.sh`).

`epd_mock.py` (the no-hardware e-paper driver) is this repository's, under the
same licence as the rest of these apps.

Bjorn depends on a number of Python libraries and command-line tools; on ZeroOS
the ones that are in Debian come from there, listed in `bjorn.pkg`'s `DEPENDS`,
each under its own licence. A few that are not in Debian are noted in the app's
README.
