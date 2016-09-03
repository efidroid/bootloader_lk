#include <mipi_dsi.h>

#include "include/panel.h"
#include "panel_display.h"

int dtbreader_init_panel_data(struct panel_struct *panelstruct,
                                struct msm_panel_info *pinfo,
                                struct mdss_dsi_phy_ctrl *phy_db);

__WEAK int dtbreader_init_panel_data_post(struct panel_struct *panelstruct,
                                struct msm_panel_info *pinfo,
                                struct mdss_dsi_phy_ctrl *phy_db)
{
    return 0;
}

int oem_panel_select(const char *panel_name, struct panel_struct *panelstruct,
                    struct msm_panel_info *pinfo,
                    struct mdss_dsi_phy_ctrl *phy_db)
{
    // build panel struct
    int paneltype = dtbreader_init_panel_data(panelstruct, pinfo, phy_db);
    if(paneltype==PANEL_TYPE_UNKNOWN) {
        return paneltype;
    }

    // apply platform changes
    int rc = dtbreader_init_panel_data_post(panelstruct, pinfo, phy_db);
    if(rc) goto err;

    return paneltype;

err:
    return PANEL_TYPE_UNKNOWN;
}
