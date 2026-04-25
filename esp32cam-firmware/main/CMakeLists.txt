# ──────────────────────────────────────────────────────────────────────────
# Main Component – Source Registration
# ──────────────────────────────────────────────────────────────────────────
idf_component_register(SRCS "main.c"
                            "src/camera_init.c"
                            "src/flash_service.c"
                            "src/server_service.c"
                            "src/wifi_init.c"
                    INCLUDE_DIRS "." "include")
