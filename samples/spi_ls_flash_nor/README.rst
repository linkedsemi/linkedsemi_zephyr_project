.. _spi_ls_flash_nor:

QSPI2 external NOR flash (qspi_ls)
**********************************

This sample tests the ``linkedsemi,qspi-ls`` driver (``qspi_ls.c``)
with ``spi_nor_multi_dev.c`` on QSPI2 (``0x40040000``, STIG mode).

Hardware
========

QSPI2 pinmux in ``boards/lsqsh_evb_cpu1.overlay`` must match the EVB wiring.
Default overlay uses PC05 (CSN), PC06–PC09 (DAT0–3), PC10 (SCK), FUNC4.

Build
=====

.. code-block:: console

	west build -p always -b lsqsh_evb@2os/lsqsh/cpu1 -d build linkedsemi_zephyr_project/samples/spi_ls_flash_nor
	or
	west build -p always -b lsqsh_evb@2os/lsqsh/cpu2 -d build_cpu2 linkedsemi_zephyr_project/samples/spi_ls_flash_nor

Expected output
===============

.. code-block:: text

[00:00:00.000,000] <inf> spi_nor: init flash2@0
[00:00:00.004,000] <inf> spi_nor: Exit QPI(4-4-4) mode
[00:00:00.070,000] <inf> spi_nor: flash2@0: SFDP v 1.10 AP ff with 3 PH
[00:00:00.077,000] <inf> spi_nor: PH0: ff00 rev 1.8: 23 DW @ 30
[00:00:00.084,000] <inf> spi_nor: flash2@0: 32 MiBy flash
[00:00:00.089,000] <inf> spi_nor: PH1: ff84 rev 1.1: 2 DW @ c0
[00:00:00.096,000] <inf> qspi_ls: qspi2@40040000: useless calibration, all capture delay ok
[00:00:00.105,000] <inf> qspi_ls: qspi2@40040000: hw config: spi=50000000 Hz cap_neg=0 cap_dly=0 (calib_combo=0x01)
*** Booting Zephyr OS build openbmc-zephyr-v0.1.0-579-gc3902d06c342 ***
jedec-id = [85 23 19];
[00:00:00.143,000] <inf> flash_spi_nor_ls_example: jedec-id = [85 23 19];

[00:00:00.151,000] <inf> flash_spi_nor_ls_example: start erase 0x0 len 0x9000
[00:00:00.563,000] <inf> flash_spi_nor_ls_example: flash2@0: Performing final bulk verification of 32896 bytes...
[00:00:00.574,000] <inf> flash_spi_nor_ls_example: flash2@0: >>> FINAL BULK CHECK PASSED! <<<
[00:00:00.585,000] <inf> flash_spi_nor_ls_example: flash2@0: >>> read_total_big_lli_buf PASSED! (Size: 8332) <<<
[00:00:00.597,000] <inf> flash_spi_nor_ls_example: flash2@0: >>> read_total_big_lli_buf1 PASSED! (Size: 8331) <<<
[00:00:00.609,000] <inf> flash_spi_nor_ls_example: flash2@0: >>> read_total_big_lli_buf2 PASSED! (Size: 8330) <<<
[00:00:00.621,000] <inf> flash_spi_nor_ls_example: flash2@0: >>> read_total_big_lli_buf3 PASSED! (Size: 8329) <<<
[00:00:00.631,000] <inf> flash_spi_nor_ls_example: flash2@0: ========== All Flash Tests Passed! ==========
