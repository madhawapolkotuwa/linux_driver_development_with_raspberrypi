import spidev
import time
import struct

# Initialize SPI
spi = spidev.SpiDev()
spi.open(0, 0) 
spi.max_speed_hz = 10000000 # Boosted to 10 MHz (MPU6500 supports up to 20MHz for data reading)

# MPU6500 Register Constants
PWR_MGMT_1 = 0x6B
ACCEL_START_REG = 0x3B

def write_register(reg, value):
    spi.xfer2([reg & 0x7F, value])

def read_burst_data(start_reg, length):
    # SPI Read: Set the MSB high (0x80) on the starting register
    # We append 'length' number of dummy bytes (0x00) to clock out the data
    tx_buf = [start_reg | 0x80] + [0x00] * length
    rx_buf = spi.xfer2(tx_buf)
    # The first returned byte is garbage (while sending the address), skip it
    return rx_buf[1:]

# 1. Wake up the MPU6500
write_register(PWR_MGMT_1, 0x00)
time.sleep(0.1)

print("Starting 14-byte burst read stream... Press Ctrl+C to stop.")

try:
    while True:
        # 2. Perform the 14-byte burst read
        # Returns: Accel (X,Y,Z), Temp, Gyro (X,Y,Z) -> 7 values total (2 bytes each)
        data = read_burst_data(ACCEL_START_REG, 14)
        
        # 3. Unpack the 14 binary bytes into 7 signed short integers (big-endian '>hhhhhhh')
        accel_x, accel_y, accel_z, temp_raw, gyro_x, gyro_y, gyro_z = struct.unpack('>hhhhhhh', bytes(data))
        
        # 4. Convert raw temperature to Celsius (per MPU6500 datasheet formula)
        temperature_c = (temp_raw / 333.87) + 21.0
        
        # Print raw values
        print(f"--- Sensor Data ---")
        print(f"Accel Raw | X: {accel_x:<6} Y: {accel_y:<6} Z: {accel_z:<6}")
        print(f"Gyro Raw  | X: {gyro_x:<6} Y: {gyro_y:<6} Z: {gyro_z:<6}")
        print(f"Temp      | {temperature_c:.2f} °C")
        
        time.sleep(0.1) # Read 10 times per second

except KeyboardInterrupt:
    print("\nStream stopped.")
finally:
    spi.close()