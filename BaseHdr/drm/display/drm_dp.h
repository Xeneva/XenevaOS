#ifndef __DRM_DISPLAY_DRM_DP_H__
#define __DRM_DISPLAY_DRM_DP_H__

/*
 * The two definitions Vendored/drivers/video/hdmi.c needs from mainline's
 * 2245-line <drm/display/drm_dp.h>: its hdmi_audio_infoframe_pack_for_dp()
 * takes a struct dp_sdp and writes 0x1b into the SDP header's HB2 byte
 * (hdmi.c:511-519).  The rest of the header is the DP link-training/register
 * surface, none of which anything in this tree names -- it was never ported,
 * and this file exists so the one include resolves without it.  Grow it when
 * a consumer asks, the same rule the other BaseHdr stubs follow.
 *
 * mainline: include/drm/display/drm_dp.h:1718, :1727, :1759.
 */

#include <linux/types.h>	/* u8, __packed -- mainline's own first include */

#define DP_SDP_AUDIO_INFOFRAME_HB2	0x1b

struct dp_sdp_header {
	u8 HB0;
	u8 HB1;
	u8 HB2;
	u8 HB3;
} __packed;

struct dp_sdp {
	struct dp_sdp_header sdp_header;
	u8 db[32];
} __packed;

#endif /* __DRM_DISPLAY_DRM_DP_H__ */
