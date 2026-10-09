QSPI 采沿校准 sample。校准在驱动里完成：低速读 JEDEC ID 做参考，再在目标频率下扫 cap_neg/cap_dly，和参考 ID 比对。

设备树只保留这两个属性，定义见 ``zephyr/dts/bindings/spi/linkedsemi,ls-qspi.yaml``：

.. code-block::

   timing-calibration-disabled;
   timing-calibration-clock-frequency = <10000000>;

``timing-calibration-disabled`` 关掉校准。不写则默认开启。
``timing-calibration-clock-frequency`` 是参考 ID 的读时钟，不写默认 10 MHz。

sample 在读完 JEDEC ID 后调用：

- ``qspi_timing_calibration_status()``：unused / success / failed
- ``qspi_timing_calibration_value()``：当前 ``cap_neg``、``cap_dly``
