# Technical references

These external references inform the board-specific notes and the future-porting brief. Check them again when updating adapters because core APIs and pin maps can change.

- Arduino-ESP32 I2S API: https://docs.espressif.com/projects/arduino-esp32/en/latest/api/i2s.html
- Arduino-ESP32 SD_MMC documentation and ESP32 pin map: https://github.com/espressif/arduino-esp32/blob/master/libraries/SD_MMC/README.md
- Arduino-ESP32 SPI SD documentation: https://github.com/espressif/arduino-esp32/blob/master/libraries/SD/README.md
- Arduino-ESP32 board/chip support listing: https://github.com/espressif/arduino-esp32/blob/master/README.md
- INMP441 product/datasheet landing page (confirm vendor and revision for electrical/data-format details): https://invensense.tdk.com/products/analog/inmp441/

The user's own serial logs are the source for the claim that their specific ESP32-CAM setup recorded and saved a WAV with GPIO12 DOUT. That observation should not be represented as a general hardware guarantee.
