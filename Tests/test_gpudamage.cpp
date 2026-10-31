#include <cassert>
#include <gpudamage.h>

int main() {
	GpuDamage damage = {};
	damage.width = 100;
	damage.height = 80;

	assert(GpuDamageAdd(&damage, 10, 20, 5, 6) == GPUSRV_OK);
	assert(GpuDamageAdd(&damage, 20, 30, 10, 8) == GPUSRV_OK);
	GpuSrvFrame frame = {};
	assert(GpuDamageTake(&damage, &frame) == GPUSRV_OK);
	assert(frame.x == 10 && frame.y == 20 && frame.width == 20 && frame.height == 18);
	assert(GpuDamageTake(&damage, &frame) == GPUSRV_E_BUSY);
	assert(GpuDamageAck(&damage, frame.sequence + 1) == GPUSRV_E_INVAL);
	assert(GpuDamageAck(&damage, frame.sequence) == GPUSRV_OK);
	assert(GpuDamageTake(&damage, &frame) == GPUSRV_E_EMPTY);

	assert(GpuDamageAdd(&damage, 0, 0, 100, 80) == GPUSRV_OK);
	assert(GpuDamageAdd(&damage, 100, 0, 1, 1) == GPUSRV_E_INVAL);
	GpuDamageReset(&damage);
	assert(GpuDamageTake(&damage, &frame) == GPUSRV_OK);
	assert(frame.x == 0 && frame.y == 0 && frame.width == 100 && frame.height == 80);
	assert(GpuDamageAck(&damage, frame.sequence) == GPUSRV_OK);
	return 0;
}
