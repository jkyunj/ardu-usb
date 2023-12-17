import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import numpy as np
from itertools import count

fig, ax = plt.subplots() 

x_data = [] # time
y_data = [] # sensor value
avg = [] # average of sensor value

index = count()

with open('/dev/ardu0', 'r') as f:
    def animate(i):
        _f = f.readline().strip()
        # read only 10 ~ 99 cm
        if(len(_f) != 2):
            return

        x_data.append(next(index))
        y_data.append(int(_f))
        avg.append(np.mean(y_data))

        plt.cla()
        plt.plot(x_data, y_data, label='distance')
        plt.plot(x_data, avg, label='average')
        plt.legend()
        plt.title('Ultrasonic sensor value', fontweight ="bold") 
        plt.xlabel('Time(ms)')
        plt.ylabel('Value(cm)')

    f.readline().strip() # discard first data

    ani = FuncAnimation(plt.gcf(), animate, interval = 1)

    plt.show()

