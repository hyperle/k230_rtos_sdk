import dart_board

address, chip_id = dart_board.camera_probe()
print("%s: I2C1 address=0x%02x, chip_id=0x%04x" %
      (dart_board.CAMERA_NAME, address, chip_id))
