#ifndef __UPNG_H__
#define __UPNG_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum upng_error {
	UPNG_EOK			= 0,
	UPNG_ENOMEM			= 1,
	UPNG_ENOTFOUND		= 2,
	UPNG_ENOTPNG		= 3,
	UPNG_EMALFORMED		= 4,
	UPNG_EUNSUPPORTED	= 5,
	UPNG_EUNINTERLACED	= 6,
	UPNG_EUNFORMAT		= 7,
	UPNG_EPARAM			= 8
} upng_error;

typedef enum upng_format {
	UPNG_BADFORMAT,
	UPNG_RGB8,
	UPNG_RGBA8,
	UPNG_LUMINANCE1,
	UPNG_LUMINANCE2,
	UPNG_LUMINANCE4,
	UPNG_LUMINANCE8,
	UPNG_LUMINANCE_ALPHA1,
	UPNG_LUMINANCE_ALPHA2,
	UPNG_LUMINANCE_ALPHA4,
	UPNG_LUMINANCE_ALPHA8
} upng_format;

typedef struct upng_t upng_t;

upng_t*			upng_new_from_bytes	(const unsigned char* buffer, unsigned long size);
void			upng_free			(upng_t* upng);

upng_error		upng_header			(upng_t* upng);
upng_error		upng_decode			(upng_t* upng);

upng_error		upng_get_error		(const upng_t* upng);
unsigned int	upng_get_error_line	(const upng_t* upng);

unsigned int	upng_get_width		(const upng_t* upng);
unsigned int	upng_get_height		(const upng_t* upng);
unsigned int	upng_get_bpp		(const upng_t* upng);
unsigned int	upng_get_bitdepth	(const upng_t* upng);
unsigned int	upng_get_components	(const upng_t* upng);
unsigned int	upng_get_pixelsize	(const upng_t* upng);
upng_format		upng_get_format		(const upng_t* upng);

const unsigned char*	upng_get_buffer		(const upng_t* upng);
unsigned int			upng_get_size		(const upng_t* upng);

#ifdef __cplusplus
}
#endif

#endif /* __UPNG_H__ */
