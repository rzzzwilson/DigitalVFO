"""
Plot the discharge.dat data.

Data file has lines:
    2018-07-14T21:57:15,7.84
"""

#import matplotlib as mpl
import matplotlib.pyplot as plt
from datetime import datetime
print(dir(datetime))

DataFile = 'xyzzy.out'

# read in raw data
x_data = []
y_data = []
starttime = 0
time = 0
with open(DataFile, 'r') as f:
    for line in f:
        (dt, volts) = line.strip().split(',')
        volts = float(volts)
        dt = datetime.strptime(dt, '%Y-%m-%dT%H:%M:%S')
        if starttime == 0:
            starttime = dt
        delta = dt - starttime
        time = delta.total_seconds() / (60 * 60)
        x_data.append(time)
        y_data.append(volts)

# plot data

# example data
fig, ax = plt.subplots()
ax.plot(x_data, y_data)
plt.xlabel('time (hours)')
plt.ylabel('volts')
ax.set_title('2S 18650 Discharge Curve - Standby')
ax.grid(True)
plt.show()
