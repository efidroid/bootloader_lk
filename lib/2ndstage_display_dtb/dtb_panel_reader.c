#include <stdlib.h>
#include <ctype.h>
#include <libfdt.h>
#include <msm_panel.h>
#include <mipi_dsi.h>
#include <lib/atagparse.h>

#include "private.h"

dtb_panel_config_t* dtbpanel_config = NULL;

static char* fdt_getprop_str(void* fdt, int offset, const char* name) {
    int len;
    const struct fdt_property* prop;

    // get property
    prop = fdt_get_property(fdt, offset, name, &len);
    if(!prop) return NULL;

    // allocate data
    char* str = malloc(len+1);
    if(!str) return NULL;

    memcpy(str, prop->data, len);
    str[len] = 0;

    return str;
}

static uint32_t fdt_getprop_u32(void* fdt, int offset, const char* name) {
    int len;
    const struct fdt_property* prop;

    // get property
    prop = fdt_get_property(fdt, offset, name, &len);
    if(!prop) return 0;

    return fdt32_to_cpu(*((uint32_t*)prop->data));
}

static int fdt_getprop_bool(void* fdt, int offset, const char* name) {
    int len;
    const struct fdt_property* prop;

    // get property
    prop = fdt_get_property(fdt, offset, name, &len);
    return !!prop;
}

static ssize_t fdt_getprop_array(void* fdt, int offset, const char* name, size_t itemsize, const void** datap) {
    int len;
    const struct fdt_property* prop;

    // get property
    prop = fdt_get_property(fdt, offset, name, &len);
    if(!prop) return -1;

    if(len%itemsize) {
        return -1;
    }

    *datap = prop->data;
    return len/itemsize;
}

static char* str2upper(char *s) {
    char* ret = s;
    for(;*s;s++) {
        *s = toupper(*s);
    }
    return ret;
}

static int trigger2int(const char* s) {
    // dsi_mdp_trigger
    int trigger_int;
    if(!strcmp(s, "none"))
        trigger_int = 0;
    else if(!strcmp(s, "trigger_te"))
        trigger_int = 2;
    else if(!strcmp(s, "trigger_sw"))
        trigger_int = 4;
    else if(!strcmp(s, "trigger_sw_seof"))
        trigger_int = 5;
    else if(!strcmp(s, "trigger_sw_te"))
        trigger_int = 6;
    else {
        dprintf(CRITICAL, "invalid trigger: %s\n", s);
        trigger_int = -1;
    }

    return trigger_int;
}

static int cmdstate2int(const char* s) {
    // dsi_mdp_trigger
    int state_int;
    if(!strcmp(s, "dsi_lp_mode"))
        state_int = 0;
    else if(!strcmp(s, "dsi_hs_mode"))
        state_int = 1;
    else {
        dprintf(CRITICAL, "invalid command state: %s\n", s);
        state_int = -1;
    }

    return state_int;
}


static int process_fdt_paneldata(void* fdt, int offset_panel, struct panel_config* paneldata) {
    int rc;
    char* destination = NULL;
    char* paneltype = NULL;

    // panel_controller
    paneldata->panel_controller = strdup("dsi:0:");
    if(!paneldata->panel_controller) { rc = -1; goto done;}
    
    // compatible
    paneldata->panel_compatible = strdup("qcom,mdss-dsi-panel");
    if(!paneldata->panel_compatible) { rc = -1; goto done;}

    // panel_type
    paneltype = fdt_getprop_str(fdt, offset_panel, "qcom,mdss-dsi-panel-type");
    if(!paneltype) { rc = -1; goto done;}
    if(!strcmp(paneltype, "dsi_video_mode"))
        paneldata->panel_type = 0;
    else if(!strcmp(paneltype, "dsi_cmd_mode"))
        paneldata->panel_type = 1;
    else {
        dprintf(CRITICAL, "invalid panel type: %s\n", paneltype);
        rc = -1;
        goto done;
    }

    // destination
    destination = fdt_getprop_str(fdt, offset_panel, "qcom,mdss-dsi-panel-destination");
    if(!destination) { rc = -1; goto done;}
    str2upper(destination);
    paneldata->panel_destination = strdup(destination);
    if(!paneldata->panel_destination) { rc = -1; goto done;}

    // TODO: panel_clockrate

    // panel_framerate;
    paneldata->panel_framerate = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-panel-framerate");

    // TODO: panel_interface
    // TODO: panel_orientation
    // TODO: panel_channelid
    // TODO: dsi_virtualchannel_id
    // TODO: panel_broadcast_mode
    // TODO: panel_lp11_init
    // TODO: panel_init_delay
    // TODO: dsi_stream
    // TODO: interleave_mode
    // TODO: panel_bitclock_freq
    // TODO: panel_operating_mode
    // TODO: panel_with_enable_gpio
    // TODO: mode_gpio_state

    // slave_panel_node_id;
    paneldata->slave_panel_node_id = strdup("");
    if(!paneldata->slave_panel_node_id) { rc = -1; goto done;}

    rc = 0;

done:
    free(destination);
    free(paneltype);

    return rc;
}

static int process_fdt_panelres(void* fdt, int offset_panel, struct panel_resolution* panelres) {
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

    return 0;
}

static int process_fdt_color(void* fdt, int offset_panel, struct color_info* color) {
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

    return 0;
}

static int process_fdt_videopanel(void* fdt, int offset_panel, struct videopanel_info* videopanel) {
    int rc;
    char* trafficmode = NULL;

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
    if(!trafficmode) { rc = -1; goto done;}
    uint32_t trafficmode_int;
    if(!strcmp(trafficmode, "non_burst_sync_pulse"))
        trafficmode_int = 0;
    else if(!strcmp(trafficmode, "non_burst_sync_event"))
        trafficmode_int = 1;
    else if(!strcmp(trafficmode, "burst_mode"))
        trafficmode_int = 2;
    else {
        dprintf(CRITICAL, "invalid trafficmode: %s\n", trafficmode);
        rc = -1;
        goto done;
    }
    videopanel->traffic_mode = trafficmode_int;

    // TODO: dma_delayafter_vsync
    // TODO: bllp_eof_power

    rc = 0;

done:
    free(trafficmode);

    return rc;
}

static int process_fdt_commandpanel(void* fdt, int offset_panel, struct commandpanel_info* commandpanel) {

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

    return 0;
}

static int process_fdt_state(void* fdt, int offset_panel, struct command_state* state) {
    int rc;
    char* state_on = NULL;
    char* state_off = NULL;

    // oncommand_state
    state_on = fdt_getprop_str(fdt, offset_panel, "qcom,mdss-dsi-on-command-state");
    if(!state_on) { rc = -1; goto done;}
    int state_on_int = cmdstate2int(state_on);
    if(state_on_int<0) {
        rc = -1;
        goto done;
    }
    state->oncommand_state = state_on_int;

    // offcommand_state
    state_off = fdt_getprop_str(fdt, offset_panel, "qcom,mdss-dsi-off-command-state");
    if(!state_off) { rc = -1; goto done;}
    int state_off_int = cmdstate2int(state_off);
    if(state_off_int<0) {
        rc = -1;
        goto done;
    }
    state->offcommand_state = state_off_int;

    rc = 0;

done:
    free(state_on);
    free(state_off);

    return rc;
}

static int process_fdt_laneconfig(void* fdt, int offset_panel, struct lane_configuration* laneconfig) {
    int rc;
    char* lane_map = NULL;

    // dsi_lanemap
    lane_map = fdt_getprop_str(fdt, offset_panel, "qcom,mdss-dsi-lane-map");
    if(lane_map) {
        int lane_map_int;
        if(!strcmp(lane_map, "lane_map_0123"))
            lane_map_int = 0;
        else if(!strcmp(lane_map, "lane_map_3012"))
            lane_map_int = 1;
        else if(!strcmp(lane_map, "lane_map_2301"))
            lane_map_int = 2;
        else if(!strcmp(lane_map, "lane_map_1230"))
            lane_map_int = 3;
        else if(!strcmp(lane_map, "lane_map_0321"))
            lane_map_int = 4;
        else if(!strcmp(lane_map, "lane_map_1032"))
            lane_map_int = 5;
        else if(!strcmp(lane_map, "lane_map_2103"))
            lane_map_int = 6;
        else if(!strcmp(lane_map, "lane_map_3210"))
            lane_map_int = 7;
        else {
            dprintf(CRITICAL, "invalid lane map: %s\n", lane_map);
            rc = -1;
            goto done;
        }
        laneconfig->dsi_lanemap = lane_map_int;
    }
    
    // lane0_state
    uint32_t dsi_lanes = 0;
    if(fdt_getprop_bool(fdt, offset_panel, "qcom,mdss-dsi-lane-0-state")) {
        dsi_lanes++;
        laneconfig->lane0_state = 1;
    }
    // lane1_state
    if(fdt_getprop_bool(fdt, offset_panel, "qcom,mdss-dsi-lane-1-state")) {
        dsi_lanes++;
        laneconfig->lane1_state = 1;
    }
    // lane2_state
    if(fdt_getprop_bool(fdt, offset_panel, "qcom,mdss-dsi-lane-2-state")) {
        dsi_lanes++;
        laneconfig->lane2_state = 1;
    }
    // lane3_state
    if(fdt_getprop_bool(fdt, offset_panel, "qcom,mdss-dsi-lane-3-state")) {
        dsi_lanes++;
        laneconfig->lane3_state = 1;
    }

    // dsi_lanes
    laneconfig->dsi_lanes = dsi_lanes;

    // TODO: force_clk_lane_hs

    rc = 0;

done:
    free(lane_map);

    return rc;
}

static int process_fdt_paneltiminginfo(void* fdt, int offset_panel, struct panel_timing* paneltiminginfo) {
    int rc;
    char* mdp_trigger = NULL;
    char* dma_trigger = NULL;

    // dsi_mdp_trigger
    mdp_trigger = fdt_getprop_str(fdt, offset_panel, "qcom,mdss-dsi-mdp-trigger");
    if(!mdp_trigger) { rc = -1; goto done;}
    int mdp_trigger_int = trigger2int(mdp_trigger);
    if(mdp_trigger_int<0) {
        rc = -1;
        goto done;
    }
    paneltiminginfo->dsi_mdp_trigger = mdp_trigger_int;

    // dsi_dma_trigger
    dma_trigger = fdt_getprop_str(fdt, offset_panel, "qcom,mdss-dsi-dma-trigger");
    if(!dma_trigger) { rc = -1; goto done;}
    int dma_trigger_int = trigger2int(dma_trigger);
    if(dma_trigger_int<0) {
        rc = -1;
        goto done;
    }
    paneltiminginfo->dsi_dma_trigger = dma_trigger_int;

    // tclk_post
    paneltiminginfo->tclk_post = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-t-clk-post");
    // tclk_pre
    paneltiminginfo->tclk_pre = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-t-clk-pre");

    rc = 0;

done:
    free(mdp_trigger);
    free(dma_trigger);

    return rc;
}

static int process_fdt_timing(void* fdt, int offset_panel, dtb_panel_config_t* config) {
    ssize_t num;
    const uint8_t *arr = NULL;
    uint32_t i;

    num = fdt_getprop_array(fdt, offset_panel, "qcom,mdss-dsi-panel-timings", sizeof(*arr), (const void**)&arr);
    if(num<0) return -1;

    config->timing = malloc(sizeof(uint32_t)*num);
    if(!config->timing) return -1;
    config->timing_len = num;

    for(i=0; i<(size_t)num; i++) {
        config->timing[i] = arr[i];
    }

    return 0;
}

static int process_fdt_backlightinfo(void* fdt, int offset_panel, struct backlight* backlightinfo) {
    int rc;
    char* controltype = NULL;

    // bl_min_level
    backlightinfo->bl_min_level = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-bl-min-level");
    // bl_max_level
    backlightinfo->bl_max_level = fdt_getprop_u32(fdt, offset_panel, "qcom,mdss-dsi-bl-max-level");

    // TODO: bl_step

    // bl_pmic_controltype
    controltype = fdt_getprop_str(fdt, offset_panel, "qcom,mdss-dsi-bl-pmic-control-type");
    if(!controltype) { rc = -1; goto done;}
    uint32_t controltype_int;
    if(!strcmp(controltype, "bl_ctrl_pwm"))
        controltype_int = 0;
    else if(!strcmp(controltype, "bl_ctrl_wled"))
        controltype_int = 1;
    else if(!strcmp(controltype, "bl_ctrl_dcs"))
        controltype_int = 2;
    else {
        dprintf(CRITICAL, "invalid controltype: %s\n", controltype);
        rc = -1;
        goto done;
    }
    backlightinfo->bl_pmic_controltype = controltype_int;

    // bl_interface_type
    backlightinfo->bl_interface_type = backlightinfo->bl_pmic_controltype;

    // bl_pmic_model
    backlightinfo->bl_pmic_model = strdup("");
    if(!backlightinfo->bl_pmic_model) { rc = -1; goto done;}

    rc = 0;

done:
    free(controltype);

    return rc;
}

static ssize_t get_num_dcs_commands(const uint8_t* stream, size_t streamsz) {
    ssize_t ret = 0;
    size_t stream_left = streamsz;
    while(stream_left>0) {
        // get command data
        const uint8_t* cmd = stream;
        if(stream_left<7) {
            dprintf(CRITICAL, "command stream is too short\n");
            return -1;
        }
        uint16_t payloadsz = cmd[5]<<16 | cmd[6];

        // get cmd size
        size_t cmdsize = 7 + payloadsz;
        if(stream_left<cmdsize) {
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

static ssize_t process_fdt_panel_commands(const uint8_t *arr, ssize_t arrsz, struct mipi_dsi_cmd** out_panel_cmds) {
    uint32_t i;

    // count dcs commands
    ssize_t num_dcs_commands = get_num_dcs_commands(arr, arrsz);
    if(num_dcs_commands<0) return -1;

    // allocate cmd array
    uint32_t panel_cmds_idx = 0;
    struct mipi_dsi_cmd* panel_cmds = calloc(sizeof(struct mipi_dsi_cmd), num_dcs_commands);
    if(!panel_cmds) return -1;

    const uint8_t* streamp = arr;
    size_t stream_left = arrsz;
    while(stream_left>0) {
        // get command data
        const uint8_t* cmd = streamp;
        if(stream_left<7) {
            dprintf(CRITICAL, "command stream is too short\n");
            return -1;
        }
        uint8_t dcstype = cmd[0];
        uint8_t wait = cmd[4];
        uint16_t payloadsz = cmd[5]<<16 | cmd[6];
        const uint8_t* payload  = &cmd[7];
        uint8_t is_lwrite = (dcstype==DTYPE_GEN_LWRITE || dcstype==DTYPE_DCS_LWRITE);

        // get cmd size
        size_t cmdsize = 7 + payloadsz;
        if(stream_left<cmdsize) {
            dprintf(CRITICAL, "command stream is too short\n");
            return -1;
        }

        // get lk payload size
        size_t lkpayloadsz;
        if(is_lwrite)
                lkpayloadsz = 4 + ROUNDUP(payloadsz, 4);
        else
                lkpayloadsz = ROUNDUP(payloadsz, 2) + 2;

        // allocate payload
        uint8_t* lkpayload = calloc(lkpayloadsz, 1);
        if(!lkpayload) return -1;
        uint32_t lkpayload_idx = 0;

        // write LWRITE hdr
        if(is_lwrite) {
            lkpayload[lkpayload_idx++] = (uint8_t)payloadsz;
            lkpayload[lkpayload_idx++] = 0x00;
            lkpayload[lkpayload_idx++] = dcstype;
            lkpayload[lkpayload_idx++] = 0xc0;
        }
        // write padding
        uint32_t alignment = is_lwrite?4:2;
        for(i=0; i<(size_t)ROUNDUP(payloadsz, alignment); i++) {
            uint8_t val = i>=payloadsz?0xff:payload[i];
            lkpayload[lkpayload_idx++] = val;
        }
        // write short write footer
        if(!is_lwrite) {
            lkpayload[lkpayload_idx++] = dcstype;
            lkpayload[lkpayload_idx++] = 0x80;
        }

        // add command to list
        struct mipi_dsi_cmd* panel_cmd = &panel_cmds[panel_cmds_idx++];
        panel_cmd->size = lkpayloadsz;
        panel_cmd->payload = (void*)lkpayload;
        panel_cmd->wait = wait;

        // skip to next command
        stream_left -= cmdsize;
        streamp += cmdsize;
    }

    *out_panel_cmds = panel_cmds;
    return num_dcs_commands;
}

static int process_fdt_on_commands(void* fdt, int offset_panel, dtb_panel_config_t* config) {
    ssize_t num;
    const uint8_t *arr = NULL;

    num = fdt_getprop_array(fdt, offset_panel, "qcom,mdss-dsi-on-command", sizeof(*arr), (const void**)&arr);
    if(num<0) return -1;

    ssize_t num_dcs_commands = process_fdt_panel_commands(arr, num, &config->on_commands);
    if(num_dcs_commands<0) return -1;
    config->num_on_commands = num_dcs_commands;

    return 0;
}

static int process_fdt_off_commands(void* fdt, int offset_panel, dtb_panel_config_t* config) {
    ssize_t num;
    const uint8_t *arr = NULL;

    num = fdt_getprop_array(fdt, offset_panel, "qcom,mdss-dsi-off-command", sizeof(*arr), (const void**)&arr);
    if(num<0) return -1;

    ssize_t num_dcs_commands = process_fdt_panel_commands(arr, num, &config->off_commands);
    if(num_dcs_commands<0) return -1;
    config->num_off_commands = num_dcs_commands;

    return 0;
}

static int process_fdt(void* fdt, const char* name, dtb_panel_config_t* config) {
    int rc;
    char buf[4096];

    snprintf(buf, sizeof(buf), "/soc/qcom,mdss_mdp/%s", name);

    int offset_panel = fdt_path_offset(fdt, buf);
    if(offset_panel<0) {
        dprintf(CRITICAL, "can't find panel node: %s\n", fdt_strerror(offset_panel));
        return -1;
    }


    // paneldata
    config->paneldata->panel_node_id = strdup(name);
    if(!config->paneldata->panel_node_id) { rc = -1; goto done;}
    rc = process_fdt_paneldata(fdt, offset_panel, config->paneldata);
    if(rc) goto done;

    // panelres
    rc = process_fdt_panelres(fdt, offset_panel, config->panelres);
    if(rc) goto done;

    // color
    rc = process_fdt_color(fdt, offset_panel, config->color);
    if(rc) goto done;

    // videopanel
    rc = process_fdt_videopanel(fdt, offset_panel, config->videopanel);
    if(rc) goto done;

    // commandpanel
    rc = process_fdt_commandpanel(fdt, offset_panel, config->commandpanel);
    if(rc) goto done;

    // state
    rc = process_fdt_state(fdt, offset_panel, config->state);
    if(rc) goto done;

    // laneconfig
    rc = process_fdt_laneconfig(fdt, offset_panel, config->laneconfig);
    if(rc) goto done;

    // paneltiminginfo
    rc = process_fdt_paneltiminginfo(fdt, offset_panel, config->paneltiminginfo);
    if(rc) goto done;

    // timing
    rc = process_fdt_timing(fdt, offset_panel, config);
    if(rc) goto done;

    // TODO: panelresetseq

    // backlightinfo
    rc = process_fdt_backlightinfo(fdt, offset_panel, config->backlightinfo);
    if(rc) goto done;

    // TODO: fbcinfo

    // on commands
    rc = process_fdt_on_commands(fdt, offset_panel, config);
    if(rc) goto done;

    // off commands
    rc = process_fdt_off_commands(fdt, offset_panel, config);
    if(rc) goto done;

    config->cont_splash_enabled = fdt_getprop_bool(fdt, offset_panel, "qcom,cont-splash-enabled");

    rc = 0;

done:
    return rc;
}

__WEAK int dtbreader_init_panel_data_post(struct panel_struct *panelstruct,
                                struct msm_panel_info *pinfo,
                                struct mdss_dsi_phy_ctrl *phy_db)
{
    return 0;
}

int dtbreader_init_panel_data(struct panel_struct *panelstruct,
                                struct msm_panel_info *pinfo,
                                struct mdss_dsi_phy_ctrl *phy_db)
{
    int rc;
    dtb_panel_config_t* config = NULL;

    config = calloc(sizeof(dtb_panel_config_t), 1);
    if(!config) goto err;

    config->paneldata = calloc(sizeof(struct panel_config), 1);
    if(!config->paneldata) goto err;

    config->panelres = calloc(sizeof(struct panel_resolution), 1);
    if(!config->panelres) goto err;

    config->color = calloc(sizeof(struct color_info), 1);
    if(!config->color) goto err;

    config->videopanel = calloc(sizeof(struct videopanel_info), 1);
    if(!config->videopanel) goto err;

    config->commandpanel = calloc(sizeof(struct commandpanel_info), 1);
    if(!config->commandpanel) goto err;

    config->state = calloc(sizeof(struct command_state), 1);
    if(!config->state) goto err;

    config->laneconfig = calloc(sizeof(struct lane_configuration), 1);
    if(!config->laneconfig) goto err;

    config->paneltiminginfo = calloc(sizeof(struct panel_timing), 1);
    if(!config->paneltiminginfo) goto err;

    config->panelresetseq = calloc(sizeof(struct panel_reset_sequence), 1);
    if(!config->panelresetseq) goto err;

    config->backlightinfo = calloc(sizeof(struct backlight), 1);
    if(!config->backlightinfo) goto err;

    rc = process_fdt(lkargs_get_tags_backup(), "qcom,mdss_dsi_mot_inx_qhd_video_v0", config);
    if(rc) {
        return PANEL_TYPE_UNKNOWN;
    }

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
    memcpy(phy_db->timing, config->timing, config->timing_len*sizeof(uint32_t));

    rc = dtbreader_init_panel_data_post(panelstruct, pinfo, phy_db);
    if(rc) goto err;

    dtbpanel_config = config;

    return PANEL_TYPE_DSI;

err:
    return PANEL_TYPE_UNKNOWN;
}
