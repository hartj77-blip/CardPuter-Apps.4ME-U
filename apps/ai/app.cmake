zs_app(ai "AI" ZS_ICON_CHIP 0xd97757 make_ai_app ORDER 60
       SOURCES ai.cpp chat.cpp)

# The AI app's requests and replies (no network).
zs_app_test(ai_test SOURCES tests/ai_test.cpp chat.cpp)
