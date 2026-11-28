# Ragnar (README.md): the Bjorn-family autonomous network recon tool, run
# headless as a background service with its web dashboard on port 8000. The
# ZeroShell module is a thin control panel (on/off, open the dashboard, who may
# reach it); the service and web UI do the work.
zs_app(ragnar "Ragnar" ZS_ICON_TOWER 0x6b4fa0 make_ragnar_app ORDER 230
       SOURCES ragnar.cpp)
