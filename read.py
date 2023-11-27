import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from itertools import count

fig, ax = plt.subplots() 

x_data = [] # time
y_data = [] # sensor value

index = count()
with open('/dev/ardu0', 'r') as f:
    f.readline().strip() # discard first data
    def animate(i):
        x_data.append(next(index))
        _f = f.readline().strip()

        y_data.append(_f)

        plt.cla()
        plt.plot(x_data, y_data)

    ani = FuncAnimation(plt.gcf(), animate, interval = 1000)
    
    plt.title('Ultrasonic sensor value', fontweight ="bold") 
    plt.xlabel('Time(s)')
    plt.ylabel('Value(cm)')
    plt.tight_layout()
    plt.show()

