# Dev Tools: one installable pack (module "dev"). Write, run and ship code
# from the keyboard: projects, REPLs, Git and a serial monitor. The shared
# plumbing (../kit, dev.cpp) is listed once, on the first app.
zs_app(code   "Code"   LV_SYMBOL_EDIT     0x2a7bde make_code_app   MODULE dev ORDER 300
       SOURCES code.cpp dev.cpp ../kit/run.cpp ../kit/ui.cpp)
zs_app(repl   "REPL"   LV_SYMBOL_KEYBOARD 0x3a8f5c make_repl_app   MODULE dev ORDER 310
       SOURCES repl.cpp)
zs_app(git    "Git"    LV_SYMBOL_SHUFFLE  0xf05033 make_git_app    MODULE dev ORDER 320
       SOURCES git.cpp)
zs_app(serial "Serial" LV_SYMBOL_USB      0x8a5cd6 make_serial_app MODULE dev ORDER 330
       SOURCES serial.cpp)
