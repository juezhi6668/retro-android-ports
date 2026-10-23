# idc for the Synaptics DSX touchscreen (Mi 6)
# verify-me: the filename must match the input device NAME (`dumpsys input` /
# getevent -p). If it is e.g. synaptics_dsx_2, add a second copy under that
# name — KitKat only applies the idc whose basename equals the device name.
# Without an idc KitKat can class the panel as a touchpad and drive a mouse
# pointer (pdx203-jellybean note).
touch.deviceType = touchScreen
touch.orientationAware = 1
touch.pressure.scale = 0.0080
