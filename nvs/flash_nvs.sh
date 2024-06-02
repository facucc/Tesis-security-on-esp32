#!/bin/sh
cd "$(dirname -- "$0")" || exit 1
 ~/esp/esp-idf/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate "./nvs/nvs.csv" ./nvs/factory_nvs.bin 0x6000

#esptool.py -p /dev/ttyUSB0 -b 460800 --before default_reset --after hard_reset --chip esp32 write_flash --flash_mode dio --flash_freq 40m --flash_size detect 0x20000 ./build/tesis.bin 0x1000 ./build/bootloader/bootloader.bin 0x8000 ./build/partition_table/partition-table.bin 0x10000 ./nvs/factory_nvs.bin