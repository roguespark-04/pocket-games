#!/bin/sh
# Host LVGL renderer for previews: ./build.sh && ./sim && python3 topng.py
cd "$(dirname "$0")"; LV=../managed_components/lvgl__lvgl; mkdir -p obj
{ find $LV/src -name '*.c'; ls ../main/ui/*.c hal_sim.c main.c; } | xargs -P16 -I{} sh -c 'o=obj/$(echo {} | tr "/." "__").o; [ -f $o ] && [ $o -nt {} ] || gcc -O1 -I. -I'$LV' -I../main/ui -I../main/hal -DLV_CONF_INCLUDE_SIMPLE -w -c {} -o $o'
gcc -o sim obj/*.o -lm
