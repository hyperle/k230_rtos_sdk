import os
import gc
import dart_board as board

print(os.uname())
print("GC free:", gc.mem_free())
print("UART (TX, RX):", board.UART_PINS)
print("I2C (SCL, SDA):", board.I2C_PINS)
print("SPI (CS, SCK, MOSI, MISO):", board.SPI_PINS)
print("PWM channel -> GPIO:", board.PWM_PINS)
print("Servo channels:", board.SERVO_CHANNELS)
print("SC038HGS CSI2 mode:", board.SC038HGS_MODE)
