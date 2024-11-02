#!/bin/bash

# Exit on any error
set -e

# Variables (default paths and options, can be overridden with environment variables)
BUILD_DIR=${BUILD_DIR:-./build}
PORT=${PORT:-/dev/ttyUSB0}
IDF_PATH=${IDF_PATH:-~/esp/esp-idf}  # Default path to ESP-IDF installation
PRIVATE_KEY=${PRIVATE_KEY:-secure_boot_signing_key.pem}
PUBLIC_KEY=${PUBLIC_KEY:-public_verification_key.pem}
SIGNED_BINARY=${SIGNED_BINARY:-./build/tesis_signed.bin}
INPUT_BINARY=${INPUT_BINARY:-./build/tesis.bin}

build() {
    echo "Building the project with ESP-IDF..."
    idf.py build
}

flash() {
    echo "Flashing the firmware..."
    esptool.py -p $PORT -b 460800 --before default_reset --after hard_reset --chip esp32 write_flash \
        --flash_mode dio --flash_freq 40m --flash_size detect \
        0x1000 $BUILD_DIR/bootloader/bootloader.bin \
        0x10000 $BUILD_DIR/partition_table/partition-table.bin \
        0x11000 nvs.bin \
        0x17000 build/ota_data_initial.bin \
        0x20000 $BUILD_DIR/tesis_signed.bin
}

monitor() {
    idf.py -p $PORT monitor 

}
# Key generation function using espsecure.py
esp_keygen() {
    echo "Generating secure boot signing key using espsecure.py..."
    espsecure.py generate_signing_key $PRIVATE_KEY
    echo "Secure boot signing key generated: $PRIVATE_KEY"
}

# Key generation function using openssl
openssl_keygen() {
    echo "Generating secure boot signing key using OpenSSL..."
    openssl ecparam -name prime256v1 -genkey -noout -out $PRIVATE_KEY
    echo "Secure boot signing key generated: $PRIVATE_KEY"
}

# Generate public key from private key
extract_public_key() {
    echo "Extracting public key"
    openssl ec -in $PRIVATE_KEY -pubout -out $PUBLIC_KEY
    echo "Public key extracted: $PUBLIC_KEY"
}

# Sign a binary file
sign_binary() {
    echo "Signing firmware binary"
    espsecure.py sign_data --keyfile $PRIVATE_KEY --output $SIGNED_BINARY $INPUT_BINARY --version 1
    echo "Firmware signed: $SIGNED_BINARY"
}

# NVS partition generation function
gen_nvs() {
    echo "Generating NVS partition..."
    python3 $IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate \
        "./nvs.csv" nvs.bin 0x6000
    echo "NVS partition generated: nvs.bin"
}

run_cppcheck() {

    REPORT_XML="cppcheck_report.xml"
    REPORT_HTML_DIR="cppcheck_html"
    SUPPRESSIONS_FILE="cppcheck-suppress"

    echo "Running Cppcheck analysis..."
    # Generate an XML report
    cppcheck --suppress=unusedFunction:main/main.c --suppress=missingIncludeSystem --enable=all --inconclusive \
        ./Components ./main --xml --xml-version=2 --verbose 2> "$REPORT_XML"

    echo "Generating HTML report from XML..."
    # Generate the HTML report using cppcheck-htmlreport
    cppcheck-htmlreport --file="$REPORT_XML" --report-dir="$REPORT_HTML_DIR" \
        --title="Cppcheck Report for ESP-IDF Project"

    echo "HTML report generated in the 'cppcheck_html' directory."
}

usage() {
    echo "Usage: $0 {build|flash|extract_public_key|sign_binary|nvs}"
    echo "Optional environment variables:"
    echo "  BUILD_DIR=<path_to_build>       (default: ./build)"
    echo "  NVS_DIR=<path_to_nvs>           (default: ./nvs)"
    echo "  PORT=<serial_port>              (default: /dev/ttyUSB0)"
    echo "  IDF_PATH=<path_to_idf>          (default: ~/esp/esp-idf)"
    echo "  PRIVATE_KEY=<path_to_private_key> (default: secure_boot_signing_key.pem)"
    echo "  PUBLIC_KEY=<path_to_public_key> (default: public_verification_key.pem)"
    exit 1
}

# Ensure the user provides a valid command
if [[ $# -eq 0 ]]; then
    usage
fi

case "$1" in
    build) build ;;
    flash) flash ;;
    monitor) monitor;;
    esp_keygen) esp_keygen ;;
    openssl_keygen) openssl_keygen ;;
    extract_public_key) extract_public_key ;;
    sign_binary) sign_binary ;;
    nvs) gen_nvs ;;
    run_cppcheck) run_cppcheck ;;
    *) usage ;;
esac