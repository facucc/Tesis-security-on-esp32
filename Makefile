# Variables
SCRIPT = ./build_and_flash.sh  # Path to your Bash script
TARGET_DIR := /home/facundo/Desktop/Tesis/Tesis-security-on-esp32
# Default target
all: help

setup:
	@chmod +x $(SCRIPT)
	@$(SCRIPT) setup

build:
	@$(SCRIPT) build

flash:
	@$(SCRIPT) flash

monitor:
	@$(SCRIPT) monitor

esp_keygen:
	@$(SCRIPT) esp_keygen

openssl_keygen:
	@$(SCRIPT) openssl_keygen

# Extract public key from private key
extract_public_key:
	@$(SCRIPT) extract_public_key

# Sign a binary file
sign_binary:
	@$(SCRIPT) sign_binary
	
# NVS partition generation target
nvs:
	@$(SCRIPT) nvs

run_cppcheck:
	@echo "Running Cppcheck on $(TARGET_DIR)..."
	$(SCRIPT) run_cppcheck $(TARGET_DIR)

help:
	@echo "Usage: make [target]"
	@echo "Available targets:"
	@echo "  setup               - Set up the environment"
	@echo "  build               - Build the project"
	@echo "  flash               - Flash the firmware to the ESP32"
	@echo "  monitor             - Monitor logs from uart"
	@echo "  esp_keygen          - Generate private signing key with espsecure.py"
	@echo "  openssl_keygen      - Generate private signing key with OpenSSL"
	@echo "  extract_public_key  - Extract public key from private key"
	@echo "  sign_binary         - Sign a binary file"
	@echo "  nvs                 - Generate NVS partition"
	@echo "  clean               - Clean temporary files"

clean:
	@echo "Cleaning temporary files..."
	@rm -f *.tmp *.bin *.html

.PHONY: all setup build flash keygen nvs help clean
