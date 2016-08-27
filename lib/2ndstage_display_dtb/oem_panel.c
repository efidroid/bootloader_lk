#include <mipi_dsi.h>

#include "include/panel.h"
#include "panel_display.h"

int dtbreader_init_panel_data(struct panel_struct *panelstruct,
                                struct msm_panel_info *pinfo,
                                struct mdss_dsi_phy_ctrl *phy_db);

int oem_panel_select(const char *panel_name, struct panel_struct *panelstruct,
                    struct msm_panel_info *pinfo,
                    struct mdss_dsi_phy_ctrl *phy_db)
{
    return dtbreader_init_panel_data(panelstruct, pinfo, phy_db);
}
