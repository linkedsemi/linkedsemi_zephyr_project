# Getting Started Guide
1.
```
$ mkdir zephyr_work
```
2.
```
$ cd zephyr_work
```
3.
```
$ git clone https://github.com/linkedsemi/linkedsemi_zephyr_project.git
```
4.
```
$ west init -l linkedsemi_zephyr_project
```
5.
```
$ west update
```
6. try to build samples in zephyr
```
$ west build -p always -b lsqsh_evb@1os/lsqsh/cpu1 zephyr/samples/hello_world
```
7. try to build samples in linkedsemi_zephyr_project
```
$ west build -p always -b lsqsh_evb@1os/lsqsh/cpu1 linkedsemi_zephyr_project/samples/reboot
```
