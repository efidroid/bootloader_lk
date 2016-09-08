#include <err.h>
#include <debug.h>
#include <string.h>
#include <msm_panel.h>
#include <mipi_dsi.h>

#include "include/panel.h"
#include "include/display_resource.h"
#include "gcdb_display.h"

#include "private.h"

int target_backlight_ctrl(struct backlight *bl, uint8_t enable)
{
    return NO_ERROR;
}

int target_panel_reset(uint8_t enable, struct panel_reset_sequence *resetseq, struct msm_panel_info *pinfo)
{
    return NO_ERROR;
}

int target_ldo_ctrl(uint8_t enable, struct msm_panel_info *pinfo)
{
    return NO_ERROR;
}

int target_cont_splash_screen(void)
{
    int enabled = 0;

    if (dtbpanel_config)
        enabled = dtbpanel_config->cont_splash_enabled;

    return enabled;
}

void target_gcdb_post_init_platform_data(struct mdss_dsi_phy_ctrl *phy_db)
{
    if (!dtbpanel_config)
        return;

    if (dtbpanel_config->panel_regulator)
        memcpy(phy_db->regulator, dtbpanel_config->panel_regulator, REGULATOR_SIZE);

    // TODO: panel_physical_ctrl

    if (dtbpanel_config->panel_strength_ctrl)
        memcpy(phy_db->strength, dtbpanel_config->panel_strength_ctrl, STRENGTH_SIZE);

    if (dtbpanel_config->panel_bist_ctrl)
        memcpy(phy_db->bistCtrl, dtbpanel_config->panel_bist_ctrl, BIST_SIZE);

    if (dtbpanel_config->panel_lane_config)
        memcpy(phy_db->laneCfg, dtbpanel_config->panel_lane_config, LANE_SIZE);
}
