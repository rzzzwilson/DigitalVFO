import serial
import time
from serial.tools.list_ports_posix import comports

#iterator = sorted(comports())
iterator = comports()
teensy_devices = []
cursor_index = 0
cursor_sign = +1

#for i in iterator:
for i in comports():
    if i.description == 'USB Serial' and i.manufacturer == 'Teensyduino':
        # we have a Teensy!
        teensy_devices.append(i.device)

if len(teensy_devices) == 1:
    ser = serial.Serial(teensy_devices[0])  # open serial port
    print(f'Opening device {ser.name}')
    for freq in range(1000000, 2000000+1, 1000):
        cmd = f'FS{freq};'.encode('latin-1')
        ser.write(cmd)                 # set device frequency
        answer = ser.readline()
        answer = answer.decode("utf-8")
        cmd = f'CS{cursor_index};'.encode('latin-1')
        ser.write(cmd)                 # set cursor position
        cursor_index += cursor_sign
        if cursor_index > 7:
            cursor_sign = -1
            cursor_index = 7
        elif cursor_index < 0:
            cursor_sign = +1
            cursor_index = 0

        time.sleep(0.25)
#        print(answer, end='')

    ser.close()
elif len(teensy_devices) == 0:
    print("Can't find aniy Teensy devices to control.")
elif len(teensy_devices) > 1:
    print("Too many Teensy devices - can't choose!")
