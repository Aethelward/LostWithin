"""Runs once when the LostWithin editor starts (Python Editor Script Plugin).

Adds Tools > LostWithin > ElevenLabs SFX menu entries. A failure here only logs a warning; it never blocks startup.
"""

import unreal


def _register_lostwithin_menus() -> None:
    try:
        import lostwithin_sfx.menu
        lostwithin_sfx.menu.register()
    except Exception as error:  # Startup must continue; the tool stays usable from the Python console.
        unreal.log_warning("LostWithin: could not add the ElevenLabs SFX menu: {}".format(error))


_register_lostwithin_menus()
