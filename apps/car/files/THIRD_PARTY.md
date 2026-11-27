# Third-party code

Car's iPod mode is two programs by Andrew Onyshchuk, built when the package is made
(`apps/car/prepare.sh` names the commits) and shipped in it. Thank you.

## ipod-gadget

The kernel driver that makes the USB port look like an iPod: https://github.com/oandrew/ipod-gadget

Built unchanged, for the one kernel ZeroOS runs, into `/usr/lib/zeroos/car-ipod/<kernel>/`. MIT
licence (`LICENSE.ipod-gadget`, beside this file); the modules declare themselves GPL to the kernel.

## ipod

The program that speaks the iPod's protocol to the car: https://github.com/oandrew/ipod

Built with one change, `apps/car/ipod/buttons.patch`, which makes it print the button the car
pressed, into `/usr/lib/zeroos/car-ipod/ipod`. MIT licence (`LICENSE.ipod`, beside this file).

It is written in Go and has these linked into it, each under its own licence (in `licenses/`,
beside this file):

- the Go standard library and runtime (BSD-3-Clause)
- github.com/sirupsen/logrus (MIT)
- github.com/urfave/cli (MIT), with github.com/cpuguy83/go-md2man (MIT),
  github.com/russross/blackfriday (BSD-2-Clause) and github.com/shurcooL/sanitized_anchor_name (MIT)
- github.com/davecgh/go-spew (ISC)
- github.com/fullsailor/pkcs7 (MIT)
- golang.org/x/crypto and golang.org/x/sys (BSD-3-Clause)
