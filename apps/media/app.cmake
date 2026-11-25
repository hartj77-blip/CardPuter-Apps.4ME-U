zs_app(media "Media" LV_SYMBOL_AUDIO 0xd6336c make_media_app ORDER 70
       SOURCES media.cpp skins.cpp winamp.cpp wsz.cpp)

# Reading Winamp skins: zip, bitmaps, colours (no screen needed).
zs_app_test(media_wsz_test SOURCES tests/wsz_test.cpp wsz.cpp)
