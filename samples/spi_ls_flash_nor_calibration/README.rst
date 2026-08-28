设备树配置：
详细见zephyr/dts/bindings/spi/linkedsemi,ls-qspi.yaml文件
	timing-calibration-auto-detect-content-disable;//这个是关闭自动校准（不加这行的，会自动校准从flash 的0地址开始）
	timing-calibration-from-end;//这个是从flash的最后往前扫描，没有这行不会从flash最后往前扫描
	timing-calibration-data-len = <4096>;//每次扫描多长字节
	timing-calibration-start-offset = <0>;//从flash的哪个地址开始扫描
	timing-calibration-per-block-len = <65536>;//步长，自动校准开启就需要自己配置步长否则默认4096
	timing-calibration-start-offset=<0>;//从flash的哪个地址开始扫描
	timing-calibration-clock-frequency = <10000000>;//校准时钟频率默认10M
推荐配置：
（1）自动校准
	timing-calibration-data-len = <4096>;//不配置默认64
	//timing-calibration-start-offset = <0>;//不配置默认从0开始校准
	timing-calibration-per-block-len = <65536>;//不配置默认4096	
	//timing-calibration-clock-frequency = <10000000>;//不配置默认10M
	//timing-calibration-start-offset=<0>;//不配置默认从0开始校准
（2）从flash的最后往前扫描4K大小，一定要关闭自动校准
	timing-calibration-auto-detect-content-disable;
	timing-calibration-from-end;
	timing-calibration-data-len = <4096>;//不配置默认64
	//timing-calibration-clock-frequency = <10000000>;//不配置默认10M
（3）自己配置校准起始位置和步长
	timing-calibration-auto-detect-content-disable;
	timing-calibration-start-offset = <0>;//不配置默认从0开始校准
	timing-calibration-per-block-len = <65536>;//不配置默认4096
	//timing-calibration-clock-frequency = <10000000>;//不配置默认10M校准频率