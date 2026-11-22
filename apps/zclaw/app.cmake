zs_app(zclaw "ZClaw" ZS_ICON_CHIP 0xd97757 make_zclaw_app ORDER 60
       SOURCES zclaw.cpp ai.cpp)

# ZClaw's AI requests and replies (no network).
zs_app_test(zclaw_test SOURCES tests/zclaw_test.cpp ai.cpp)
