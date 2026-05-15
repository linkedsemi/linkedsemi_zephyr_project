## build

```
app_core: west build -p always -d build_app -b lsqsh_evb@2os/lsqsh/cpu2 ./linkedsemi_zephyr_project/samples/usb_acm_agent/app_core -DDTC_OVERLAY_FILE="app.overlay"
sec_core: west build -p always -d build_sec -b lsqsh_evb@2os/lsqsh/cpu1 ./linkedsemi_zephyr_project/samples/usb_acm_agent/sec_core -DDTC_OVERLAY_FILE="app.overlay"
```

## run
```
(gdb) file build_sec/zephyr/zephyr.elf
(gdb) load
(gdb) restore ./build_app/zephyr/zephyr.bin binary 0x10000000
(gdb) c
```