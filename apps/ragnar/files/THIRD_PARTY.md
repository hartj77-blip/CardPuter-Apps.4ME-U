# Third-party code

This app packages **Ragnar** and runs it headless on ZeroOS. None of the code
below is this repository's; it is fetched at build time by `prepare.sh` at a
pinned commit and installed unmodified.

| Project | Author | Licence | Used for |
| --- | --- | --- | --- |
| [Ragnar](https://github.com/PierreGode/Ragnar) | PierreGode | MIT | The whole application: the headless service (`headlessRagnar.py`), the web dashboard, the scanners and probes |
| [Bjorn](https://github.com/infinition/Bjorn) | infinition | MIT | Ragnar is a fork of Bjorn; the core design and much of the code originate there |

Ragnar's `headlessRagnar.py` entrypoint (the no-display, web-only mode this
package relies on) is credited in its own header to DezusAZ.

Pinned commit: `313ee7e34a1d3b554a3e7e04561c9d9297a17022` (see `prepare.sh`).

Ragnar in turn depends on a number of Python libraries and command-line tools;
on ZeroOS those come from Debian, listed in `ragnar.pkg`'s `DEPENDS`, each under
its own licence.
