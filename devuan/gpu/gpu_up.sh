#!/bin/sh
# gpu_up.sh — bring GPU-stakken op efter boot (logd + servicemanager + pvrsrvctl)
mkdir -p /dev/socket && chmod 777 /dev/socket
pgrep -f "bin/log[d]" >/dev/null || nohup /system/bin/logd >/root/logd_console.log 2>&1 &
pgrep -f "servicemanage[r]" >/dev/null || nohup /system/bin/servicemanager --standalone >/root/sm_console.log 2>&1 &
sleep 1
/system/vendor/bin/pvrsrvctl --start
echo "GPU-stak: pvrsrvctl-exit=$?"
mkdir -p /dev/graphics
for i in 0 1 2 3; do [ -e /dev/graphics/fb$i ] || ln -s /dev/fb$i /dev/graphics/fb$i; done
chmod 666 /dev/graphics/fb* 2>/dev/null
