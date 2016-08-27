#ifndef PRIVATE_H
#define PRIVATE_H

#include "include/panel.h"
#include "panel_display.h"

/* dcs read/write */
#define DTYPE_DCS_WRITE        0x05    /* short write, 0 parameter */
#define DTYPE_DCS_WRITE1    0x15    /* short write, 1 parameter */
#define DTYPE_DCS_READ        0x06    /* read */
#define DTYPE_DCS_LWRITE    0x39    /* long write */

/* generic read/write */
#define DTYPE_GEN_WRITE        0x03    /* short write, 0 parameter */
#define DTYPE_GEN_WRITE1    0x13    /* short write, 1 parameter */
#define DTYPE_GEN_WRITE2    0x23    /* short write, 2 parameter */
#define DTYPE_GEN_LWRITE    0x29    /* long write */
#define DTYPE_GEN_READ        0x04    /* long read, 0 parameter */
#define DTYPE_GEN_READ1        0x14    /* long read, 1 parameter */
#define DTYPE_GEN_READ2        0x24    /* long read, 2 parameter */

typedef struct {
    struct panel_config         *paneldata;
    struct panel_resolution     *panelres;
    struct color_info           *color;
    struct videopanel_info      *videopanel;
    struct commandpanel_info    *commandpanel;
    struct command_state        *state;
    struct lane_configuration   *laneconfig;
    struct panel_timing         *paneltiminginfo;
    struct panel_reset_sequence *panelresetseq;
    struct backlight            *backlightinfo;
    struct fb_compression        fbcinfo;

    uint32_t* timing;
    size_t timing_len;

    struct mipi_dsi_cmd* on_commands;
    size_t num_on_commands;
    struct mipi_dsi_cmd* off_commands;
    size_t num_off_commands;

    int cont_splash_enabled;
} dtb_panel_config_t;

extern dtb_panel_config_t* dtbpanel_config;

#endif // PRIVATE_H
