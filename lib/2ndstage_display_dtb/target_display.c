#include <err.h>
#include <debug.h>
#include <msm_panel.h>

#include "include/panel.h"
#include "include/display_resource.h"

#include "private.h"

int target_backlight_ctrl(struct backlight *bl, uint8_t enable) {
    return NO_ERROR;
}

int target_panel_reset(uint8_t enable, struct panel_reset_sequence *resetseq, struct msm_panel_info *pinfo) {
    return NO_ERROR;
}

int target_ldo_ctrl(uint8_t enable, struct msm_panel_info *pinfo) {
    return NO_ERROR;
}

int target_cont_splash_screen(void) {
    int enabled = 0;

    if(dtbpanel_config) 
        enabled = dtbpanel_config->cont_splash_enabled;

    return enabled;
}
