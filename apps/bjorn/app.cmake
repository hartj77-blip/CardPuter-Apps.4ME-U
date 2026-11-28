# Bjorn (README.md): the original Bjorn autonomous network recon tool, run
# headless as a background service with its web dashboard on port 8000. The
# ZeroShell module is a thin control panel (on/off, open the dashboard, who may
# reach it); the service and web UI do the work.
zs_app(bjorn "Bjorn" ZS_ICON_NETWORK 0xb5493b make_bjorn_app ORDER 228
       SOURCES bjorn.cpp)
