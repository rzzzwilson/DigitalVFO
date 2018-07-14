"""
Plot the discharge.dat data.
"""

#import matplotlib as mpl
import matplotlib.pyplot as plt

DataFile = 'discharge.dat'

# read in raw data
x_data = []
y_data = []
time = 0
with open(DataFile, 'r') as f:
    for line in f:
        (date, hour, percent) = line.strip().split(',')
#        print((date, hour, percent))
        percent = int(percent)
        x_data.append(time)
        time += 0.5
        y_data.append(percent)

# plot data

# example data
fig, ax = plt.subplots()
ax.plot(x_data, y_data)
plt.xlabel('time (hours)')
plt.ylabel('percent charge')
ax.set_title('2S 18650 Discharge')
ax.grid(True)
plt.show()
