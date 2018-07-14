"""
Plot the charge.dat data.
"""

#import matplotlib as mpl
import matplotlib.pyplot as plt

DataFile = 'charge.dat'

# read in raw data
t_data = []
psv_data = []
psi_data = []
volts_data = []
percent_data = []
time = 0
with open(DataFile, 'r') as f:
    # date       hour PSv  PSi v    %
    # 2018-07-13,1745,8.39,344,7.82,101
    for line in f:
        (date, hour, psv, psi, volts, percent) = line.strip().split(',')
        psv = float(psv)
        psi = int(psi)
        volts = float(volts)
        percent = int(percent)
        t_data.append(time)
        time += 0.25
        psv_data.append(psv)
        psi_data.append(psi)
        volts_data.append(volts)
        percent_data.append(percent)

# plot data
fig, ax = plt.subplots()
plt.subplot(4, 1, 1)
plt.plot(t_data, psv_data, label='PSv')
plt.title('2S 18650 Charge')
plt.ylabel('PSv (V)')

plt.subplot(4, 1, 2)
plt.plot(t_data, psi_data, label='PSi')
#plt.title('2S 18650 Charge')
plt.ylabel('PSi (mA)')

plt.subplot(4, 1, 3)
plt.plot(t_data, volts_data, label='volts')
#plt.title('2S 18650 Charge')
plt.ylabel('volts')

plt.subplot(4, 1, 4)
plt.plot(t_data, percent_data, label='percent charge')
#plt.title('2S 18650 Charge')
plt.ylabel('charge %')
plt.xlabel('time (hours)')
ax.grid(True)


#plt.xlabel('time (hours)')
#plt.ylabel('percent charge')
#ax.legend(loc='top left')

plt.show()
