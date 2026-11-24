zs_app(browser "Browser" ZS_ICON_GLOBE 0x3584e4 make_browser_app ORDER 65
       SOURCES browser.cpp page.cpp)

# The page engine: Markdown, HTML, layout and addresses (no network, no LVGL).
zs_app_test(browser_test SOURCES tests/page_test.cpp page.cpp)
