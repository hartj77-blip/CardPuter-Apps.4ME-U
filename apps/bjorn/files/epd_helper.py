# epd_helper.py -- ZeroOS headless build.
#
# This is Bjorn's epd_helper.py with one change: when the configured Waveshare
# driver can't be imported (no e-paper HAT, so no RPi.GPIO/spidev/SPI device),
# it falls back to the mock driver (resources/waveshare_epd/epd_mock.py) instead
# of raising. Bjorn then renders web/screen.png as normal -- the web UI and the
# Bjorn face work -- with no hardware. Everything else is unchanged.
#
# The configured epd_type is left as a real model (e.g. epd2in13_V4) on purpose:
# shared.py only sets screen_reversed / web_screen_reversed for known models, so
# an unknown value like "epd_mock" would leave those unset and crash display.py.
# Keeping a real model name means those flags are set, while this loader quietly
# uses the mock for the actual (absent) panel.

import importlib
import logging

logger = logging.getLogger(__name__)

class EPDHelper:
    def __init__(self, epd_type):
        self.epd_type = epd_type
        self.epd = self._load_epd_module()

    def _load_epd_module(self):
        if self.epd_type != 'epd_mock':
            try:
                epd_module_name = f'resources.waveshare_epd.{self.epd_type}'
                epd_module = importlib.import_module(epd_module_name)
                return epd_module.EPD()
            except Exception as e:
                # No e-paper HAT on ZeroOS: fall back to the headless mock so
                # Bjorn runs web-only instead of crashing at startup.
                logger.warning(f"EPD {self.epd_type} unavailable ({e}); using headless mock")
        from resources.waveshare_epd import epd_mock
        return epd_mock.EPD()

    def init_full_update(self):
        try:
            if hasattr(self.epd, 'FULL_UPDATE'):
                self.epd.init(self.epd.FULL_UPDATE)
            elif hasattr(self.epd, 'lut_full_update'):
                self.epd.init(self.epd.lut_full_update)
            else:
                self.epd.init()
            logger.info("EPD full update initialization complete.")
        except Exception as e:
            logger.error(f"Error initializing EPD for full update: {e}")
            raise

    def init_partial_update(self):
        try:
            if hasattr(self.epd, 'PART_UPDATE'):
                self.epd.init(self.epd.PART_UPDATE)
            elif hasattr(self.epd, 'lut_partial_update'):
                self.epd.init(self.epd.lut_partial_update)
            else:
                self.epd.init()
            logger.info("EPD partial update initialization complete.")
        except Exception as e:
            logger.error(f"Error initializing EPD for partial update: {e}")
            raise

    def display_partial(self, image):
        try:
            if hasattr(self.epd, 'displayPartial'):
                self.epd.displayPartial(self.epd.getbuffer(image))
            else:
                self.epd.display(self.epd.getbuffer(image))
            logger.info("Partial display update complete.")
        except Exception as e:
            logger.error(f"Error during partial display update: {e}")
            raise

    def clear(self):
        try:
            self.epd.Clear()
            logger.info("EPD cleared.")
        except Exception as e:
            logger.error(f"Error clearing EPD: {e}")
            raise
