#include <zephyr/kernel.h>

int main(void)
{
	while (true) {
		k_msleep(1000);
	}
	return 0;
}
