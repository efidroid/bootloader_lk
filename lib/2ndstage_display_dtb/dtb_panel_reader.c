#include <stdlib.h>
#include <ctype.h>
#include <libfdt.h>
#include <msm_panel.h>
#include <mipi_dsi.h>
#include <lib/atagparse.h>
#include <lib/cmdline.h>
#include <lib/hex2unsigned.h>

#include "gcdb_display.h"
#include "private.h"

#ifndef WITH_KERNEL_UEFIAPI
#include "fastboot.h"
#endif

dtb_panel_config_t *dtbpanel_config = NULL;

static char *safe_strdup(const char *s)
{
    char *ret = strdup(s);
    ASSERT(ret);

    return ret;
}

static void *safe_malloc(size_t size)
{
    char *ret = malloc(size);
    ASSERT(ret);

    return ret;
}

static void *safe_calloc(size_t num, size_t size)
{
    char *ret = calloc(num, size);
    ASSERT(ret);

    return ret;
}

static char *fdt_getprop_str(const void *fdt, int offset, const char *name)
{
    int len;
    const struct fdt_property *prop;

    // get property
    prop = fdt_get_property(fdt, offset, name, &len);
    if (!prop) return NULL;

    // allocate data
    char *str = safe_malloc(len+1);

    memcpy(str, prop->data, len);
    str[len] = 0;

    return str;
}

static uint32_t fdt_getprop_u32(const void *fdt, int offset, const char *name)
{
    int len;
    const struct fdt_property *prop;

    // get property
    prop = fdt_get_property(fdt, offset, name, &len);
    if (!prop) return 0;

    return fdt32_to_cpu(*((uint32_t *)prop->data));
}

static int fdt_getprop_bool(const void *fdt, int offset, const char *name)
{
    int len;
    const struct fdt_property *prop;

    // get property
    prop = fdt_get_property(fdt, offset, name, &len);
    return !!prop;
}

static ssize_t fdt_getprop_array(const void *fdt, int offset, const char *name, size_t itemsize, const void **datap)
{
    int len;
    const struct fdt_property *prop;

    // get property
    prop = fdt_get_property(fdt, offset, name, &len);
    if (!prop) return -1;

    if (len%itemsize) {
        return -1;
    }

    *datap = prop->data;
    return len/itemsize;
}

static char *str2upper(char *s)
{
    char *ret = s;
    for (; *s; s++) {
        *s = toupper(*s);
    }
    return ret;
}

static int trigger2int(const char *s)
{
    // dsi_mdp_trigger
    int trigger_int;
    if (!strcmp(s, "none"))
        trigger_int = 0;
    else if (!strcmp(s, "trigger_te"))
        trigger_int = 2;
    else if (!strcmp(s, "trigger_sw"))
        trigger_int = 4;
    else if (!strcmp(s, "trigger_sw_seof"))
        trigger_int = 5;
    else if (!strcmp(s, "trigger_sw_te"))
        trigger_int = 6;
    else {
        dprintf(CRITICAL, "invalid trigger: %s\n", s);
        trigger_int = -1;
    }

    return trigger_int;
}

static int cmdstate2int(const char *s)
{
    // dsi_mdp_trigger
    int state_int;
    if (!strcmp(s, "dsi_lp_mode"))
        state_int = 0;
    else if (!strcmp(s, "dsi_hs_mode"))
        state_int = 1;
    else {
        dprintf(CRITICAL, "invalid command state: %s\n", s);
        state_int = -1;
    }

    return state_int;
}

static ssize_t get_num_dcs_commands(const uint8_t *stream, size_t streamsz)
{
    ssize_t ret = 0;
    size_t stream_left = streamsz;
    while (stream_left>0) {
        // get command data
        const uint8_t *cmd = stream;
        if (stream_left<7) {
            dprintf(CRITICAL, "command stream is too short\n");
            return -1;
        }
        uint16_t payloadsz = cmd[5]<<16 | cmd[6];

        // get cmd size
        size_t cmdsize = 7 + payloadsz;
        if (stream_left<cmdsize) {
            dprintf(CRITICAL, "command stream is too short\n");
            return -1;
        }

        ret++;

        // skip to next command
        stream_left -= cmdsize;
        stream += cmdsize;
    }

    return ret;
}

static ssize_t process_fdt_panel_commands(const uint8_t *arr, ssize_t arrsz, struct mipi_dsi_cmd **out_panel_cmds)
{
    uint32_t i;

    // count dcs commands
    ssize_t num_dcs_commands = get_num_dcs_commands(arr, arrsz);
    if (num_dcs_commands<0) return -1;

    // allocate cmd array
    uint32_t panel_cmds_idx = 0;
    struct mipi_dsi_cmd *panel_cmds = safe_calloc(sizeof(struct mipi_dsi_cmd), num_dcs_commands);

    const uint8_t *streamp = arr;
    size_t stream_left = arrsz;
    while (stream_left>0) {
        // get command data
        const uint8_t *cmd = streamp;
        if (stream_left<7) {
            dprintf(CRITICAL, "command stream is too short\n");
            return -1;
        }
        uint8_t dcstype = cmd[0];
        uint8_t wait = cmd[4];
        uint16_t payloadsz = cmd[5]<<16 | cmd[6];
        const uint8_t *payload  = &cmd[7];
        uint8_t is_lwrite = (dcstype==DTYPE_GEN_LWRITE || dcstype==DTYPE_DCS_LWRITE);

        // get cmd size
        size_t cmdsize = 7 + payloadsz;
        if (stream_left<cmdsize) {
            dprintf(CRITICAL, "command stream is too short\n");
            return -1;
        }

        // get lk payload size
        size_t lkpayloadsz;
        if (is_lwrite)
            lkpayloadsz = 4 + ROUNDUP(payloadsz, 4);
        else
            lkpayloadsz = ROUNDUP(payloadsz, 2) + 2;

        // allocate payload
        uint8_t *lkpayload = safe_calloc(lkpayloadsz, 1);
        uint32_t lkpayload_idx = 0;

        // write LWRITE hdr
        if (is_lwrite) {
            lkpayload[lkpayload_idx++] = (uint8_t)payloadsz;
            lkpayload[lkpayload_idx++] = 0x00;
            lkpayload[lkpayload_idx++] = dcstype;
            lkpayload[lkpayload_idx++] = 0xc0;
        }
        // write padding
        uint32_t alignment = is_lwrite?4:2;
        for (i=0; i<(size_t)ROUNDUP(payloadsz, alignment); i++) {
            uint8_t val = i>=payloadsz?0xff:payload[i];
            lkpayload[lkpayload_idx++] = val;
        }
        // write short write footer
        if (!is_lwrite) {
            lkpayload[lkpayload_idx++] = dcstype;
            lkpayload[lkpayload_idx++] = 0x80;
        }

        // add command to list
        struct mipi_dsi_cmd *panel_cmd = &panel_cmds[panel_cmds_idx++];
        panel_cmd->size = lkpayloadsz;
        panel_cmd->payload = (void *)lkpayload;
        panel_cmd->wait = wait;

        // skip to next command
        stream_left -= cmdsize;
        streamp += cmdsize;
    }

    *out_panel_cmds = panel_cmds;
    return num_dcs_commands;
}

static int process_fdt_commands(const void *fdt, int offset_panel, dtb_panel_config_t *config, const char *nodename, size_t *out_num, struct mipi_dsi_cmd **out_cmds)
{
    ssize_t num;
    const uint8_t *arr = NULL;

    num = fdt_getprop_array(fdt, offset_panel, nodename, sizeof(*arr), (const void **)&arr);
    if (num<0) return -1;

    ssize_t num_dcs_commands = process_fdt_panel_commands(arr, num, out_cmds);
    if (num_dcs_commands<0) return -1;
    *out_num = num_dcs_commands;

    return 0;
}


static void process_fdt_paneldata(const void *fdt, int offset_panel, struct panel_config *paneldata, const char* nodename)
{
    char *destination = NULL;
    char *paneltype = NULL;

    // panel_controller
    paneldata->panel_controller = safe_strdup("dsi:0:");

    // compatible
    paneldata->panel_compatible = safe_strdup("qcom,mdss-dsi-panel");

    // panel_type
    paneltype = fdt_getprop_str(fdt, offset_panel, "qcom,mdss-dsi-panel-type");
    if (paneltype) {
        if (!strcmp(paneltype, "dsi_video_mode"))
            paneldata->panel_type = 0;
        else if (!strcmp(paneltype, "dsi_cmd_mode"))
            paneldata->panel_type = 1;
        else {
            dprintf(CRITICAL, "invalid panel type: %s\n", paneltype);
        }
    }

    // destination
    destination = fdt_getprop_str(fdt, offset_panel, "qcom,mdss-dsi-panel-destination");
    if (destination) {
        str2upper(destination);
        paneldata->panel_destination = safe_strdup(destination);
    }

    // TODO: panel_clockrate

    // panel_framerate;
    paneldata->panel_framerate = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-panel-framerate");

    // TODO: panel_interface
    // TODO: panel_orientation
    // TODO: panel_channelid

    // dsi_virtualchannel_id
    paneldata->dsi_virtualchannel_id = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-virtual-channel-id");

    // TODO: panel_broadcast_mode

    // panel_lp11_init
    paneldata->panel_lp11_init = fdt_getprop_bool(fdt, offset_panel, "qcom,mdss-dsi-lp11-init");

    // TODO: panel_init_delay

    // dsi_stream
    paneldata->dsi_stream = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-stream");

    // TODO: interleave_mode
    // TODO: panel_bitclock_freq
    // TODO: panel_operating_mode
    // TODO: panel_with_enable_gpio
    // TODO: mode_gpio_state

    // slave_panel_node_id;
    paneldata->slave_panel_node_id = safe_strdup(nodename);

    free(destination);
    free(paneltype);
}

static void process_fdt_panelres(const void *fdt, int offset_panel, struct panel_resolution *panelres)
{
    // panel_width
    panelres->panel_width = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-panel-width");
    // panel_height
    panelres->panel_height = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-panel-height");
    // hfront_porch
    panelres->hfront_porch = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-h-front-porch");
    // hback_porch
    panelres->hback_porch = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-h-back-porch");
    // hpulse_width
    panelres->hpulse_width = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-h-pulse-width");
    // hsync_skew
    panelres->hsync_skew = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-h-sync-skew");
    // vfront_porch
    panelres->vfront_porch = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-v-front-porch");
    // vback_porch
    panelres->vback_porch = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-v-back-porch");
    // vpulse_width
    panelres->vpulse_width = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-v-pulse-width");
    // hleft_border
    panelres->hleft_border = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-h-left-border");
    // hright_border
    panelres->hright_border = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-h-right-border");
    // vtop_border
    panelres->vtop_border = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-v-top-border");
    // vbottom_border
    panelres->vbottom_border = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-v-bottom-border");

    // TODO: hactive_res
    // TODO: vactive_res
    // TODO: invert_data_polarity
    // TODO: invert_vsync_polarity
    // TODO: invert_hsync_polarity
}

static void process_fdt_color(const void *fdt, int offset_panel, struct color_info *color)
{
    //  color_format
    color->color_format = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-bpp");
    //  color_order
    color->color_order = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-color-order");
    //  underflow_color
    color->underflow_color = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-underflow-color");
    //  border_color
    color->border_color = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-border-color");
    //  pixel_packing
    color->pixel_packing = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-pixel-packing");

    // TODO: pixel_alignment
}

static void process_fdt_videopanel(const void *fdt, int offset_panel, struct videopanel_info *videopanel)
{
    char *trafficmode = NULL;

    // hsync_pulse
    videopanel->hsync_pulse = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-h-sync-pulse");

    // hfp_power_mode
    videopanel->hfp_power_mode = fdt_getprop_bool(fdt, offset_panel, "qcom,mdss-dsi-hfp-power-mode");
    // hbp_power_mode
    videopanel->hbp_power_mode = fdt_getprop_bool(fdt, offset_panel, "qcom,mdss-dsi-hbp-power-mode");
    // hsa_power_mode
    videopanel->hsa_power_mode = fdt_getprop_bool(fdt, offset_panel, "qcom,mdss-dsi-hsa-power-mode");
    // bllp_eof_power_mode
    videopanel->bllp_eof_power_mode = fdt_getprop_bool(fdt, offset_panel, "qcom,mdss-dsi-bllp-eof-power-mode");
    // bllp_power_mode
    videopanel->bllp_power_mode = fdt_getprop_bool(fdt, offset_panel, "qcom,mdss-dsi-bllp-power-mode");

    // traffic_mode
    trafficmode = fdt_getprop_str(fdt, offset_panel, "qcom,mdss-dsi-traffic-mode");
    if (trafficmode) {
        uint32_t trafficmode_int = 0;
        if (!strcmp(trafficmode, "non_burst_sync_pulse"))
            trafficmode_int = 0;
        else if (!strcmp(trafficmode, "non_burst_sync_event"))
            trafficmode_int = 1;
        else if (!strcmp(trafficmode, "burst_mode"))
            trafficmode_int = 2;
        else {
            dprintf(CRITICAL, "invalid trafficmode: %s\n", trafficmode);
        }
        videopanel->traffic_mode = trafficmode_int;
    }

    // TODO: dma_delayafter_vsync
    // TODO: bllp_eof_power

    free(trafficmode);
}

static void process_fdt_commandpanel(const void *fdt, int offset_panel, struct commandpanel_info *commandpanel)
{

    // techeck_enable
    commandpanel->techeck_enable = fdt_getprop_bool(fdt, offset_panel, "qcom,mdss-dsi-te-check-enable");
    // tepin_select
    commandpanel->tepin_select = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-te-pin-select");
    // teusing_tepin
    commandpanel->teusing_tepin = fdt_getprop_bool(fdt, offset_panel, "qcom,mdss-dsi-te-using-te-pin");

    // TODO: autorefresh_enable
    // TODO: autorefresh_framenumdiv

    // tevsync_rdptr_irqline
    commandpanel->tevsync_rdptr_irqline = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-te-v-sync-rd-ptr-irq-line");
    // tevsync_continue_lines
    commandpanel->tevsync_continue_lines = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-te-v-sync-continues-lines");
    // tevsync_startline_divisor
    commandpanel->tevsync_startline_divisor = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-te-v-sync-start-line-divisor");
    // tepercent_variance
    commandpanel->tepercent_variance = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-te-percent-variance");
    // tedcs_command
    commandpanel->tedcs_command = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-te-dcs-command");

    // TODO: disable_eotafter_hsxfer
    // TODO: cmdmode_idletime
}

static void process_fdt_state(const void *fdt, int offset_panel, struct command_state *state)
{
    char *state_on = NULL;
    char *state_off = NULL;

    // oncommand_state
    state_on = fdt_getprop_str(fdt, offset_panel, "qcom,mdss-dsi-on-command-state");
    if (state_on) {
        int state_on_int = cmdstate2int(state_on);
        if (state_on_int>=0) {
            state->oncommand_state = state_on_int;
        }
    }

    // offcommand_state
    state_off = fdt_getprop_str(fdt, offset_panel, "qcom,mdss-dsi-off-command-state");
    if (state_off) {
        int state_off_int = cmdstate2int(state_off);
        if (state_off_int>=0) {
            state->offcommand_state = state_off_int;
        }
    }

    free(state_on);
    free(state_off);
}

static void process_fdt_laneconfig(const void *fdt, int offset_panel, struct lane_configuration *laneconfig)
{
    char *lane_map = NULL;

    // dsi_lanemap
    lane_map = fdt_getprop_str(fdt, offset_panel, "qcom,mdss-dsi-lane-map");
    if (lane_map) {
        int lane_map_int = 0;
        if (!strcmp(lane_map, "lane_map_0123"))
            lane_map_int = 0;
        else if (!strcmp(lane_map, "lane_map_3012"))
            lane_map_int = 1;
        else if (!strcmp(lane_map, "lane_map_2301"))
            lane_map_int = 2;
        else if (!strcmp(lane_map, "lane_map_1230"))
            lane_map_int = 3;
        else if (!strcmp(lane_map, "lane_map_0321"))
            lane_map_int = 4;
        else if (!strcmp(lane_map, "lane_map_1032"))
            lane_map_int = 5;
        else if (!strcmp(lane_map, "lane_map_2103"))
            lane_map_int = 6;
        else if (!strcmp(lane_map, "lane_map_3210"))
            lane_map_int = 7;
        else {
            dprintf(CRITICAL, "invalid lane map: %s\n", lane_map);
        }
        laneconfig->dsi_lanemap = lane_map_int;
    }

    // lane0_state
    uint32_t dsi_lanes = 0;
    if (fdt_getprop_bool(fdt, offset_panel, "qcom,mdss-dsi-lane-0-state")) {
        dsi_lanes++;
        laneconfig->lane0_state = 1;
    }
    // lane1_state
    if (fdt_getprop_bool(fdt, offset_panel, "qcom,mdss-dsi-lane-1-state")) {
        dsi_lanes++;
        laneconfig->lane1_state = 1;
    }
    // lane2_state
    if (fdt_getprop_bool(fdt, offset_panel, "qcom,mdss-dsi-lane-2-state")) {
        dsi_lanes++;
        laneconfig->lane2_state = 1;
    }
    // lane3_state
    if (fdt_getprop_bool(fdt, offset_panel, "qcom,mdss-dsi-lane-3-state")) {
        dsi_lanes++;
        laneconfig->lane3_state = 1;
    }

    // dsi_lanes
    laneconfig->dsi_lanes = dsi_lanes;

    // TODO: force_clk_lane_hs

    free(lane_map);
}

static void process_fdt_paneltiminginfo(const void *fdt, int offset_panel, struct panel_timing *paneltiminginfo)
{
    char *mdp_trigger = NULL;
    char *dma_trigger = NULL;

    // dsi_mdp_trigger
    mdp_trigger = fdt_getprop_str(fdt, offset_panel, "qcom,mdss-dsi-mdp-trigger");
    if (mdp_trigger) {
        int mdp_trigger_int = trigger2int(mdp_trigger);
        if (mdp_trigger_int>=0) {
            paneltiminginfo->dsi_mdp_trigger = mdp_trigger_int;
        }
    }

    // dsi_dma_trigger
    dma_trigger = fdt_getprop_str(fdt, offset_panel, "qcom,mdss-dsi-dma-trigger");
    if (dma_trigger) {
        int dma_trigger_int = trigger2int(dma_trigger);
        if (dma_trigger_int>=0) {
            paneltiminginfo->dsi_dma_trigger = dma_trigger_int;
        }
    }

    // tclk_post
    paneltiminginfo->tclk_post = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-t-clk-post");
    // tclk_pre
    paneltiminginfo->tclk_pre = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-t-clk-pre");

    free(mdp_trigger);
    free(dma_trigger);
}

static void process_fdt_timing(const void *fdt, int offset_panel, dtb_panel_config_t *config)
{
    ssize_t num;
    const uint8_t *arr = NULL;
    uint32_t i;

    num = fdt_getprop_array(fdt, offset_panel, "qcom,mdss-dsi-panel-timings", sizeof(*arr), (const void **)&arr);
    if (num<0) return;

    config->timing = safe_malloc(sizeof(uint32_t)*num);
    config->timing_len = num;

    for (i=0; i<(size_t)num; i++) {
        config->timing[i] = arr[i];
    }
}

static void process_fdt_backlightinfo(const void *fdt, int offset_panel, struct backlight *backlightinfo)
{
    char *controltype = NULL;

    // bl_min_level
    backlightinfo->bl_min_level = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-bl-min-level");
    // bl_max_level
    backlightinfo->bl_max_level = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-bl-max-level");

    // TODO: bl_step

    // bl_pmic_controltype
    controltype = fdt_getprop_str(fdt, offset_panel, "qcom,mdss-dsi-bl-pmic-control-type");
    if (controltype) {
        uint32_t controltype_int = 0;
        if (!strcmp(controltype, "bl_ctrl_pwm"))
            controltype_int = 0;
        else if (!strcmp(controltype, "bl_ctrl_wled"))
            controltype_int = 1;
        else if (!strcmp(controltype, "bl_ctrl_dcs"))
            controltype_int = 2;
        else {
            dprintf(CRITICAL, "invalid controltype: %s\n", controltype);
        }
        backlightinfo->bl_pmic_controltype = controltype_int;
    }

    // bl_interface_type
    backlightinfo->bl_interface_type = backlightinfo->bl_pmic_controltype;

    // bl_pmic_model
    backlightinfo->bl_pmic_model = safe_strdup("");

    free(controltype);
}

static void process_fdt_platform_regulator(const void *fdt, int offset, dtb_panel_config_t *config, const char *nodename)
{
    ssize_t num;
    const uint8_t *arr = NULL;
    uint32_t i;

    if (config->panel_lane_config)
        return;

    num = fdt_getprop_array(fdt, offset, nodename, sizeof(*arr), (const void **)&arr);
    if (num<0) return;
    if (num<6) {
        dprintf(CRITICAL, "invalid size for '%s': %lu\n", nodename, num);
        return;
    }

    config->panel_regulator = safe_malloc(sizeof(uint32_t)*num);
    for (i=0; i<(size_t)num; i++) {
        config->panel_regulator[i] = arr[i];
    }
}

static void process_fdt_platform_strength_ctrl(const void *fdt, int offset, dtb_panel_config_t *config, const char *nodename)
{
    ssize_t num;
    const uint8_t *arr = NULL;
    uint32_t i;

    if (config->panel_lane_config)
        return;

    num = fdt_getprop_array(fdt, offset, nodename, sizeof(*arr), (const void **)&arr);
    if (num<0) return;
    if (num!=2) {
        dprintf(CRITICAL, "invalid size for '%s': %lu\n", nodename, num);
        return;
    }

    config->panel_strength_ctrl = safe_malloc(sizeof(uint32_t)*2);
    for (i=0; i<(size_t)num; i++) {
        config->panel_strength_ctrl[i] = arr[i];
    }
}

static void process_fdt_platform_bist_ctrl(const void *fdt, int offset, dtb_panel_config_t *config, const char *nodename)
{
    ssize_t num;
    const uint8_t *arr = NULL;
    uint32_t i;

    if (config->panel_lane_config)
        return;

    num = fdt_getprop_array(fdt, offset, nodename, sizeof(*arr), (const void **)&arr);
    if (num<0) return;
    if (num!=6) {
        dprintf(CRITICAL, "invalid size for '%s': %lu\n", nodename, num);
        return;
    }

    config->panel_bist_ctrl = safe_malloc(sizeof(uint8_t)*6);
    for (i=0; i<6; i++) {
        config->panel_bist_ctrl[i] = arr[i];
    }
}

static void process_fdt_platform_laneconfig(const void *fdt, int offset, dtb_panel_config_t *config, const char *nodename)
{
    ssize_t num;
    const uint8_t *arr = NULL;
    uint32_t i, j;

    if (config->panel_lane_config)
        return;

    num = fdt_getprop_array(fdt, offset, nodename, sizeof(*arr), (const void **)&arr);
    if (num<0) return;

    // 4 lanes + clk lane configuration
    if (num!=45 && num!=30) {
        dprintf(CRITICAL, "invalid size for '%s': %lu\n", nodename, num);
        return;
    }

    config->panel_lane_config = safe_malloc(sizeof(uint8_t)*45);

    // lane config n * (0 - 4) & DataPath setup
    // CFG0, CFG1, CFG2, CFG3, TEST_DATAPATH, TEST_STR0, TEST_STR1, TEST_STR2, TEST_STR3
    if (num==45) {
        for (i=0; i<5; i++) {
            for (j=0; j<9; j++) {
                config->panel_lane_config[i*9 + j] = arr[i*9 + j];
            }
        }
    }

    // CFG0, CFG1, CFG2, TEST_DATAPATH, TEST_STR0, TEST_STR1
    else if (num==30) {
        for (i=0; i<5; i++) {
            config->panel_lane_config[i*9 + 0] = arr[i*6 + 0]; // CFG0
            config->panel_lane_config[i*9 + 1] = arr[i*6 + 1]; // CFG1
            config->panel_lane_config[i*9 + 2] = arr[i*6 + 2]; // CFG2
            config->panel_lane_config[i*9 + 3] = 0x00;         // CFG3
            config->panel_lane_config[i*9 + 4] = arr[i*6 + 3]; // TEST_DATAPATH
            config->panel_lane_config[i*9 + 5] = arr[i*6 + 4]; // TEST_STR0
            config->panel_lane_config[i*9 + 6] = arr[i*6 + 5]; // TEST_STR1
            config->panel_lane_config[i*9 + 7] = 0x00;         // TEST_STR2
            config->panel_lane_config[i*9 + 8] = 0x00;         // TEST_STR3
        }
    }
}

static int process_fdt(const void *fdt, const char *name, dtb_panel_config_t *config)
{
    char buf[4096];

    snprintf(buf, sizeof(buf), "/soc/qcom,mdss_mdp/%s", name);

    int offset_panel = fdt_path_offset(fdt, buf);
    if (offset_panel<0) {
        dprintf(CRITICAL, "can't find panel node: %s\n", fdt_strerror(offset_panel));
        return -1;
    }

    int offset_mdss_dsi = fdt_path_offset(fdt, "/soc/qcom,mdss_dsi");

    // paneldata
    config->paneldata->panel_node_id = safe_strdup(name);
    process_fdt_paneldata(fdt, offset_panel, config->paneldata, name);

    // panelres
    process_fdt_panelres(fdt, offset_panel, config->panelres);

    // color
    process_fdt_color(fdt, offset_panel, config->color);

    // videopanel
    process_fdt_videopanel(fdt, offset_panel, config->videopanel);

    // commandpanel
    process_fdt_commandpanel(fdt, offset_panel, config->commandpanel);

    // state
    process_fdt_state(fdt, offset_panel, config->state);

    // laneconfig
    process_fdt_laneconfig(fdt, offset_panel, config->laneconfig);

    // paneltiminginfo
    process_fdt_paneltiminginfo(fdt, offset_panel, config->paneltiminginfo);

    // timing
    process_fdt_timing(fdt, offset_panel, config);

    // TODO: panelresetseq

    // backlightinfo
    process_fdt_backlightinfo(fdt, offset_panel, config->backlightinfo);

    // TODO: fbcinfo

    // on commands
    process_fdt_commands(fdt, offset_panel, config, "qcom,mdss-dsi-on-command", &config->num_on_commands, &config->on_commands);

    // off commands
    process_fdt_commands(fdt, offset_panel, config, "qcom,mdss-dsi-off-command", &config->num_off_commands, &config->off_commands);

    // sony init commands
    process_fdt_commands(fdt, offset_panel, config, "somc,mdss-dsi-early-init-command", &config->num_earlyinit_commands, &config->earlyinit_commands);
    process_fdt_commands(fdt, offset_panel, config, "somc,mdss-dsi-init-command", &config->num_init_commands, &config->init_commands);

    // platform: regulator
    process_fdt_platform_regulator(fdt, offset_mdss_dsi, config, "qcom,platform-regulator-settings");

    // platform: strength ctrl
    process_fdt_platform_strength_ctrl(fdt, offset_mdss_dsi, config, "qcom,platform-strength-ctrl");

    // platform: bist ctrl
    process_fdt_platform_bist_ctrl(fdt, offset_mdss_dsi, config, "qcom,platform-bist-ctrl");

    // platform: lane config
    process_fdt_platform_laneconfig(fdt, offset_panel, config, "somc,mdss-dsi-lane-config");
    process_fdt_platform_laneconfig(fdt, offset_panel, config, "qcom,panel-phy-laneConfig");
    process_fdt_platform_laneconfig(fdt, offset_mdss_dsi, config, "qcom,platform-lane-config");

    // cont splash
    config->cont_splash_enabled = fdt_getprop_bool(fdt, offset_panel, "qcom,cont-splash-enabled");
    config->hw_vsync_mode = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-hw-vsync-mode");

    return 0;
}

static int startswith(const char *str, const char *pre)
{
    return strncmp(pre, str, strlen(pre)) == 0;
}

static int sony_panel_is_compatible(const void *fdt, int node, uint32_t lcdid_adc)
{
    ssize_t num;
    const uint32_t *arr = NULL;

    num = fdt_getprop_array(fdt, node, "somc,lcd-id-adc", sizeof(*arr), (const void **)&arr);
    if (num<0 || num!=2)
        return 0;

    if(lcdid_adc>=arr[0] && lcdid_adc<=arr[1])
        return 1;

    return 0;
}

#define MAX_LEVEL	32		/* how deeply nested we will go */
static int sony_panel_by_id(const void *fdt, int node, uint32_t lcdid_adc, const char** nodename)
{
	int nextoffset;		/* next node offset from libfdt */
	uint32_t tag;		/* current tag */
	int level = 0;		/* keep track of nesting level */
	const char *pathp;
	int depth = 1;		/* the assumed depth of this node */

	while (level >= 0) {
		tag = fdt_next_tag(fdt, node, &nextoffset);
		switch (tag) {
		case FDT_BEGIN_NODE:
			if (level <= depth) {
				if (level == 1) {
                    if(sony_panel_is_compatible(fdt, node, lcdid_adc)) {
                        if(nodename) {
                            pathp = fdt_get_name(fdt, node, NULL);
			                if (pathp == NULL)
				                pathp = "/* NULL pointer error */";
			                if (*pathp == '\0')
				                pathp = "/";	/* root is nameless */

                            *nodename = pathp;
                        }

                        return node;
                    }
                }
			}
			level++;
			if (level >= MAX_LEVEL) {
				dprintf(CRITICAL, "Nested too deep, aborting.\n");
				return -FDT_ERR_NOTFOUND;
			}
			break;
		case FDT_END_NODE:
			level--;
			if (level == 0)
				level = -1;		/* exit the loop */
			break;
		case FDT_END:
			return -FDT_ERR_NOTFOUND;
		case FDT_PROP:
			break;
		default:
			if (level <= depth)
				dprintf(CRITICAL, "Unknown tag 0x%08X\n", tag);
			return -FDT_ERR_NOTFOUND;
		}
		node = nextoffset;
	}
	return -FDT_ERR_NOTFOUND;
}

static char* sony_get_panel_nodename(const void *fdt) {
    const char *value = cmdline_get(lkargs_get_command_line_list(), "lcdid_adc");
    if (!value) return NULL;
    uint32_t lcdid_adc = efidroid_hex2unsigned(value);

    int offset_soc = fdt_path_offset(fdt, "/soc/qcom,mdss_mdp");
    if (offset_soc<0) {
        dprintf(CRITICAL, "can't find soc node: %s\n", fdt_strerror(offset_soc));
        return NULL;
    }

    const char* nodename = NULL;
    sony_panel_by_id(fdt, offset_soc, lcdid_adc, &nodename);
    if(nodename)
        return safe_strdup(nodename);

    return NULL;
}

int dtbreader_init_panel_data(struct panel_struct *panelstruct,
                              struct msm_panel_info *pinfo,
                              struct mdss_dsi_phy_ctrl *phy_db)
{
    int rc;
    dtb_panel_config_t *config = NULL;

    // allocate data
    config = safe_calloc(sizeof(dtb_panel_config_t), 1);
    config->pinfo = pinfo;
    config->paneldata = safe_calloc(sizeof(struct panel_config), 1);
    config->panelres = safe_calloc(sizeof(struct panel_resolution), 1);
    config->color = safe_calloc(sizeof(struct color_info), 1);
    config->videopanel = safe_calloc(sizeof(struct videopanel_info), 1);
    config->commandpanel = safe_calloc(sizeof(struct commandpanel_info), 1);
    config->state = safe_calloc(sizeof(struct command_state), 1);
    config->laneconfig = safe_calloc(sizeof(struct lane_configuration), 1);
    config->paneltiminginfo = safe_calloc(sizeof(struct panel_timing), 1);
    config->panelresetseq = safe_calloc(sizeof(struct panel_reset_sequence), 1);
    config->backlightinfo = safe_calloc(sizeof(struct backlight), 1);

    // get panel node name
    char *panel_node = lkargs_get_panel_name("mdss_mdp.panel");
    if (!panel_node) {
        panel_node = sony_get_panel_nodename(lkargs_get_tags_backup());
    }
    if (!panel_node) {
        dprintf(CRITICAL, "can't find panel node\n");
        return PANEL_TYPE_UNKNOWN;
    }

    // process fdt
    rc = process_fdt(lkargs_get_tags_backup(), panel_node, config);
    if (rc) {
        dprintf(CRITICAL, "process_fdt failed\n");
        return PANEL_TYPE_UNKNOWN;
    }

    // check if we have commands
    if (!config->num_on_commands || !config->off_commands) {
        dprintf(CRITICAL, "no on/off commands found\n");
        return PANEL_TYPE_UNKNOWN;
    }

    // build lk panel struct
    panelstruct->paneldata    = config->paneldata;
    panelstruct->panelres     = config->panelres;
    panelstruct->color        = config->color;
    panelstruct->videopanel   = config->videopanel;
    panelstruct->commandpanel = config->commandpanel;
    panelstruct->state        = config->state;
    panelstruct->laneconfig   = config->laneconfig;
    panelstruct->paneltiminginfo = config->paneltiminginfo;
    panelstruct->panelresetseq = config->panelresetseq;
    panelstruct->backlightinfo = config->backlightinfo;
    memcpy(&panelstruct->fbcinfo, &config->fbcinfo, sizeof(config->fbcinfo));
    pinfo->mipi.panel_on_cmds = config->on_commands;
    pinfo->mipi.num_of_panel_on_cmds = config->num_on_commands;
    pinfo->mipi.panel_off_cmds = config->off_commands;
    pinfo->mipi.num_of_panel_off_cmds = config->num_off_commands;
    if (config->timing)
        memcpy(phy_db->timing, config->timing, config->timing_len*sizeof(uint32_t));

    // merge sony commands into one
    if (config->num_earlyinit_commands || config->num_init_commands) {
        size_t num_on_commands_merged = config->num_earlyinit_commands + config->num_init_commands + config->num_on_commands;
        struct mipi_dsi_cmd *on_commands_merged = calloc(sizeof(struct mipi_dsi_cmd), num_on_commands_merged);

        struct mipi_dsi_cmd *cmdptr = on_commands_merged;
        memcpy(cmdptr, config->earlyinit_commands, config->num_earlyinit_commands*sizeof(*cmdptr));
        cmdptr += config->num_earlyinit_commands;

        memcpy(cmdptr, config->init_commands, config->num_init_commands*sizeof(*cmdptr));
        cmdptr += config->num_init_commands;

        memcpy(cmdptr, config->on_commands, config->num_on_commands*sizeof(*cmdptr));
        cmdptr += config->num_on_commands;

        pinfo->mipi.panel_on_cmds = on_commands_merged;
        pinfo->mipi.num_of_panel_on_cmds = num_on_commands_merged;
    }

    // set global config variable
    dtbpanel_config = config;

    return PANEL_TYPE_DSI;
}

#ifndef WITH_KERNEL_UEFIAPI
#define PRINT2OUTBUF(fmt, ...) do { \
    size_t __bytes_written = sprintf(outbuf, (fmt), ##__VA_ARGS__); \
    if(__bytes_written>0) { \
        outbuf += __bytes_written; \
    } \
} while(0)

char* print_panel_commands(char *outbuf, const char* prefix, const char* type, struct mipi_dsi_cmd* panel_commands, size_t num) {
    uint32_t i, j;

    for(i=0; i<num; i++) {
        struct mipi_dsi_cmd* panel_cmd = &panel_commands[i];
        PRINT2OUTBUF("static char %s_%s_cmd%u[] = {\n", prefix, type, i);
        for(j=0; j<panel_cmd->size; j++) {
            uint8_t* payload = (uint8_t*)panel_cmd->payload;
            if(j%4==0)
                PRINT2OUTBUF("\t");
            else
                PRINT2OUTBUF(", ");
            PRINT2OUTBUF("0x%02X", payload[j]);

            if(j%4==3)
                PRINT2OUTBUF(",\n");

        }
        PRINT2OUTBUF("};\n\n");
    }
    PRINT2OUTBUF("static struct mipi_dsi_cmd %s_%s_command[] = {\n", prefix, type);
    for(i=0; i<num; i++) {
        struct mipi_dsi_cmd* panel_cmd = &panel_commands[i];
        PRINT2OUTBUF("\t{0x%x, %s_%s_cmd%u, 0x%02x},\n", panel_cmd->size, prefix, type, i, panel_cmd->wait);
    }
    PRINT2OUTBUF("};\n");

    char* typeupper = str2upper(strdup(type));
    char* prefixupper = str2upper(strdup(prefix));
    PRINT2OUTBUF("\n#define %s_%s_COMMAND %u\n", prefixupper, typeupper, num);
    free(prefixupper);
    free(typeupper);

    return outbuf;
}

void cmd_oem_dump_panelheader(const char *arg, void *data, unsigned sz) {
    uint32_t i;

    dtb_panel_config_t* config = dtbpanel_config;
    if (!dtbpanel_config) {
        fastboot_fail("no config found");
        return ;
    }

    struct panel_config* paneldata = config->paneldata;
    struct panel_resolution* panelres = config->panelres;
    struct color_info* color = config->color;
    struct videopanel_info* videopanel = config->videopanel;
    struct commandpanel_info* commandpanel = config->commandpanel;
    struct command_state* state = config->state;
    struct lane_configuration* laneconfig = config->laneconfig;
    struct panel_timing* paneltiminginfo = config->paneltiminginfo;
    struct panel_reset_sequence* panelresetseq = config->panelresetseq;
    struct backlight* backlightinfo = config->backlightinfo;
    struct fb_compression* fbcinfo = &config->fbcinfo;

    const char* prefix = arg;
    char* prefixupper = str2upper(strdup(prefix));
    char* outbuf = (char*) data;

    PRINT2OUTBUF("#ifndef _PANEL_%s_H_\n", prefixupper);
    PRINT2OUTBUF("#define _PANEL_%s_H_\n", prefixupper);
    PRINT2OUTBUF("/*---------------------------------------------------------------------------*/\n");
    PRINT2OUTBUF("/* HEADER files                                                              */\n");
    PRINT2OUTBUF("/*---------------------------------------------------------------------------*/\n");
    PRINT2OUTBUF("#include \"panel.h\"\n");

    if(paneldata) {
        PRINT2OUTBUF("\n/*---------------------------------------------------------------------------*/\n");
        PRINT2OUTBUF("/* Panel configuration                                                       */\n");
        PRINT2OUTBUF("/*---------------------------------------------------------------------------*/\n");
        PRINT2OUTBUF("static struct panel_config %s_panel_data = {\n", prefix);
        PRINT2OUTBUF("\t\"%s\", \"%s\", \"%s\",\n",
            paneldata->panel_node_id, paneldata->panel_controller, paneldata->panel_compatible
        );
        PRINT2OUTBUF("\t%u, %u, \"%s\", %u, 0x%08x, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, \"%s\"\n",
            paneldata->panel_interface, paneldata->panel_type, paneldata->panel_destination,
            paneldata->panel_orientation, paneldata->panel_clockrate, paneldata->panel_framerate, paneldata->panel_channelid,
            paneldata->dsi_virtualchannel_id, paneldata->panel_broadcast_mode, paneldata->panel_lp11_init, paneldata->panel_init_delay,
            paneldata->dsi_stream, paneldata->interleave_mode, paneldata->panel_bitclock_freq,
            paneldata->panel_operating_mode, paneldata->panel_with_enable_gpio, paneldata->mode_gpio_state,
            paneldata->slave_panel_node_id
        );
        PRINT2OUTBUF("};\n");
    }

    if(panelres) {
        PRINT2OUTBUF("\n/*---------------------------------------------------------------------------*/\n");
        PRINT2OUTBUF("/* Panel resolution                                                          */\n");
        PRINT2OUTBUF("/*---------------------------------------------------------------------------*/\n");
        PRINT2OUTBUF("static struct panel_resolution %s_panel_res = {\n", prefix);
        PRINT2OUTBUF("\t%u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u\n",
            panelres->panel_width, panelres->panel_height, panelres->hfront_porch, panelres->hback_porch,
            panelres->hpulse_width, panelres->hsync_skew, panelres->vfront_porch, panelres->vback_porch,
            panelres->vpulse_width, panelres->hleft_border, panelres->hright_border, panelres->vtop_border,
            panelres->vbottom_border, panelres->hactive_res, panelres->vactive_res,
            panelres->invert_data_polarity, panelres->invert_vsync_polarity, panelres->invert_hsync_polarity
        );
        PRINT2OUTBUF("};\n");
    }

    if(color) {
        PRINT2OUTBUF("\n/*---------------------------------------------------------------------------*/\n");
        PRINT2OUTBUF("/* Panel color information                                                   */\n");
        PRINT2OUTBUF("/*---------------------------------------------------------------------------*/\n");
        PRINT2OUTBUF("static struct color_info %s_color = {\n", prefix);
        PRINT2OUTBUF("\t%u, %u, 0x%x, %u, %u, %u\n",
            color->color_format, color->color_order, color->underflow_color, color->border_color,
            color->pixel_packing, color->pixel_alignment
        );
        PRINT2OUTBUF("};\n");
    }

    // panel commands
    PRINT2OUTBUF("\n/*---------------------------------------------------------------------------*/\n");
    PRINT2OUTBUF("/* Panel on/off command information                                          */\n");
    PRINT2OUTBUF("/*---------------------------------------------------------------------------*/\n");
    if(config->on_commands)
        outbuf = print_panel_commands(outbuf, prefix, "on", config->on_commands, config->num_on_commands);
    PRINT2OUTBUF("\n\n");
    if(config->off_commands)
        outbuf = print_panel_commands(outbuf, prefix, "off", config->off_commands, config->num_off_commands);
    PRINT2OUTBUF("\n");

    if(state) {
        // state
        PRINT2OUTBUF("\nstatic struct command_state %s_state = {\n", prefix);
        PRINT2OUTBUF("\t%u, %u\n",
            state->oncommand_state, state->offcommand_state
        );
        PRINT2OUTBUF("};\n");
    }

    if(commandpanel) {
        // commandpanel
        PRINT2OUTBUF("\n/*---------------------------------------------------------------------------*/\n");
        PRINT2OUTBUF("/* Command mode panel information                                            */\n");
        PRINT2OUTBUF("/*---------------------------------------------------------------------------*/\n");
        PRINT2OUTBUF("static struct commandpanel_info %s_command_panel = {\n", prefix);
        PRINT2OUTBUF("\t%u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u\n",
            commandpanel->techeck_enable, commandpanel->tepin_select, commandpanel->teusing_tepin,
            commandpanel->autorefresh_enable, commandpanel->autorefresh_framenumdiv,
            commandpanel->tevsync_rdptr_irqline, commandpanel->tevsync_continue_lines, commandpanel->tevsync_startline_divisor,
            commandpanel->tepercent_variance, commandpanel->tedcs_command, commandpanel->disable_eotafter_hsxfer,
            commandpanel->cmdmode_idletime
        );
        PRINT2OUTBUF("};\n");
    }

    if(videopanel) {
        // videopanel
        PRINT2OUTBUF("\n/*---------------------------------------------------------------------------*/\n");
        PRINT2OUTBUF("/* Video mode panel information                                              */\n");
        PRINT2OUTBUF("/*---------------------------------------------------------------------------*/\n");
        PRINT2OUTBUF("static struct videopanel_info %s_video_panel = {\n", prefix);
        PRINT2OUTBUF("\t%u, %u, %u, %u, %u, %u, %u, %u, %u\n",
            videopanel->hsync_pulse, videopanel->hfp_power_mode, videopanel->hbp_power_mode, videopanel->hsa_power_mode,
            videopanel->bllp_eof_power_mode, videopanel->bllp_power_mode, videopanel->traffic_mode,
            videopanel->dma_delayafter_vsync, videopanel->bllp_eof_power
        );
        PRINT2OUTBUF("};\n");
    }

    if(laneconfig) {
        // laneconfig
        PRINT2OUTBUF("\n/*---------------------------------------------------------------------------*/\n");
        PRINT2OUTBUF("/* Lane configuration                                                        */\n");
        PRINT2OUTBUF("/*---------------------------------------------------------------------------*/\n");
        PRINT2OUTBUF("static struct lane_configuration %s_lane_config = {\n", prefix);
        PRINT2OUTBUF("\t%u, %u, %u, %u, %u, %u, %u\n",
            laneconfig->dsi_lanes, laneconfig->dsi_lanemap,
            laneconfig->lane0_state, laneconfig->lane1_state, laneconfig->lane2_state, laneconfig->lane3_state,
            laneconfig->force_clk_lane_hs
        );
        PRINT2OUTBUF("};\n");
    }

    if(config->timing) {
        // paneltiminginfo
        PRINT2OUTBUF("\n/*---------------------------------------------------------------------------*/\n");
        PRINT2OUTBUF("/* Panel timing                                                              */\n");
        PRINT2OUTBUF("/*---------------------------------------------------------------------------*/\n");

        PRINT2OUTBUF("static const uint32_t %s_timings[] = {\n", prefix);
        PRINT2OUTBUF("\t");
        for(i=0; i<config->timing_len; i++) {
            if(i!=0)
                PRINT2OUTBUF(", ");
            PRINT2OUTBUF("0x%02x", config->timing[i]);
        }
        PRINT2OUTBUF("\n};\n");
    }

    if(paneltiminginfo) {
        PRINT2OUTBUF("\nstatic struct panel_timing %s_timing_info = {\n", prefix);
        PRINT2OUTBUF("\t%u, %u, 0x%02x, 0x%02x\n",
            paneltiminginfo->dsi_mdp_trigger, paneltiminginfo->dsi_dma_trigger,
            paneltiminginfo->tclk_post, paneltiminginfo->tclk_pre
        );
        PRINT2OUTBUF("};\n");
    }

    // TODO: panelresetseq

    if(backlightinfo) {
        // backlightinfo
        PRINT2OUTBUF("\n/*---------------------------------------------------------------------------*/\n");
        PRINT2OUTBUF("/* Backlight setting                                                         */\n");
        PRINT2OUTBUF("/*---------------------------------------------------------------------------*/\n");
        PRINT2OUTBUF("static struct backlight %s_backlight = {\n", prefix);
        PRINT2OUTBUF("\t%u, %u, %u, %u, %u, \"%s\"\n",
            backlightinfo->bl_interface_type, backlightinfo->bl_min_level, backlightinfo->bl_max_level,
            backlightinfo->bl_step, backlightinfo->bl_pmic_controltype, backlightinfo->bl_pmic_model
        );
        PRINT2OUTBUF("};\n");
    }

    // TODO: fbcinfo

    PRINT2OUTBUF("\n#endif /*_PANEL_%s_H_*/", prefixupper);

    free(prefixupper);
    fastboot_send_string_human(data, (size_t)(outbuf-(char*)data));
    fastboot_okay("");
}
#endif
