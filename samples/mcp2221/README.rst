## build

```
810: west build -p always -b lsqsh_evb@1os/lsqsh/cpu1 ./linkedsemi_zephyr_project/samples/mcp2221 -DDTC_OVERLAY_FILE="lsqsh_evb_cpu1.overlay;usbd_fs.overlay"


610: west build -p always -b lsqsh_evb@1os/lsqsh/cpu1 ./linkedsemi_zephyr_project/samples/mcp2221 -DDTC_OVERLAY_FILE="usbd_fs.overlay"
```

