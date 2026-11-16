# The analytics modules (csi_dsp, csi_ncc, vitals, gait_id, count_est,
# motion_gate, person_vitals, activity, csi_gain) are ESP-IDF free and always compiled; a
# feature switched off in menuconfig simply never calls them, and
# --gc-sections drops the code.
#
# esp_driver_usb_serial_jtag + vfs: the Step 10 serial console installs the
# USB-Serial-JTAG driver and switches the VFS (printf) onto it. vfs is named
# explicitly because driver/usb_serial_jtag_vfs.h includes esp_vfs.h, and
# the driver component only exports vfs when the console is on USJ.
idf_component_register(SRCS "main.c" "csi_dsp.c" "csi_ncc.c" "vitals.c" "gait_id.c" "count_est.c"
                            "motion_gate.c" "person_vitals.c" "activity.c" "csi_gain.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_netif nvs_flash lwip esp_event esp_timer esp_driver_gpio
                                esp_driver_usb_serial_jtag vfs)
