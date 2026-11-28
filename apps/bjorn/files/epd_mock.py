# epd_mock.py -- a no-hardware e-paper driver for headless Bjorn on ZeroOS.
#
# Bjorn is written for a Waveshare 2.13" e-Paper HAT. ZeroShell owns the
# Cardputer's screen, so there is no HAT here. This driver gives Bjorn's
# EPDHelper the interface it expects while touching no hardware and importing
# neither RPi.GPIO nor spidev, so Bjorn runs on any Linux box.
#
# Nothing is lost from the web UI: Bjorn's Display still renders its PIL image
# and writes web/screen.png every frame, so the Bjorn face and status animate
# in the dashboard exactly as on a real panel. Only the physical-panel push is a
# no-op.
#
# Selected by setting "epd_type": "epd_mock" (prepare.sh sets it as the default).
import logging

logger = logging.getLogger(__name__)


class EPD:
    # Match epd2in13_V4's geometry so Bjorn's layouts fit unchanged.
    width = 122
    height = 250

    def __init__(self):
        logger.info("epd_mock: headless e-paper (no hardware); web screen only")

    # EPDHelper calls init(mode) or init(); accept and ignore any argument.
    def init(self, *args, **kwargs):
        return 0

    # EPDHelper does display(getbuffer(image)); the real driver packs the image
    # to the panel's 1bpp buffer. Headless needs no packing: pass it through.
    def getbuffer(self, image):
        return image

    def display(self, *args, **kwargs):
        return

    def displayPartial(self, *args, **kwargs):
        return

    def displayPartBaseImage(self, *args, **kwargs):
        return

    def Clear(self, *args, **kwargs):
        return

    def sleep(self, *args, **kwargs):
        return

    def Dev_exit(self, *args, **kwargs):
        return

    def module_exit(self, *args, **kwargs):
        return
