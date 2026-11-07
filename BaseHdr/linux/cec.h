/*
 * DCL cec.h -- one macro.
 *
 * drm_edid.c includes this name, and exactly one CEC symbol from it is used
 * rather than none: drm_edid.c:6714 writes
 *
 *     info->source_physical_address = CEC_PHYS_ADDR_INVALID;
 *
 * into the drm_connector's cec sink info when a block read fails. Every other
 * cec_* path is CONFIG_CEC-gated upstream and has no counterpart here, so this
 * is the whole header.
 *
 * 0xffff is mainline's value (include/media/cec.h), and it is chosen so the
 * "no address" state is not a legal one: the physical address is a 16-bit
 * four-nibble tree path, and each nibble is 0..0xe -- so no valid address has
 * a set top nibble, and 0xffff cannot be confused with a real position in the
 * HDMI topology. drm_connector.h:916 documents the same default.
 */
#ifndef _LINUX_CEC_H
#define _LINUX_CEC_H

#define CEC_PHYS_ADDR_INVALID	0xffff

#endif /* _LINUX_CEC_H */
