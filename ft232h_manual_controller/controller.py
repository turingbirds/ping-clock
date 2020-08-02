"""
https://learn.adafruit.com/adafruit-ft232h-with-spi-and-i2c-libraries/spi-devices
https://cdn-learn.adafruit.com/downloads/pdf/adafruit-ft232h-with-spi-and-i2c-libraries.pdf

https://eblot.github.io/pyftdi/api/spi.html
https://github.com/eblot/pyftdi/blob/master/pyftdi/doc/api/spi.rst
https://github.com/eblot/pyftdi/blob/master/pyftdi/spi.py

"""
import subprocess
import re
import math
import time
import numpy as np
from pyftdi.spi import SpiController, SpiIOError
from struct import *

# FT232H pinout:
# AD0 --- SCLK
# AD1 --- MOSI
# AD2 --- MISO
# AD3 --- CS0

def ping_to_angle(ping_ms: float) -> float:
	"""
	Ping     angle returned
	.1 ms    below_0p1_ms_section_angle
	1 ms     0.
	1000 ms  1.
	"""
	below_0p1_ms_section_angle = .082
	if ping_ms < 1:
		# compress the range 0.1..1
		ping_ms = max(.1, ping_ms)
		return below_0p1_ms_section_angle * math.log10(ping_ms)

	if ping_ms > 1000:
		# compress the range 1000..10000
		ping_ms = min(ping_ms, 10000)
		return ping_to_angle(1000) + below_0p1_ms_section_angle * (math.log10(ping_ms) - math.log10(1000))

	return math.log10(ping_ms) / 3


def set_word(target_phase, stepsize, direction):
	word = [0, 0, 0, 0, 0, 0]
	word[0] = target_phase & 0x000000FF
	word[1] = (target_phase & 0x0000FF00) >> 8
	word[2] = (target_phase & 0x00FF0000) >> 16
	word[3] = (target_phase & 0xFF000000) >> 24
	word[4] = stepsize
	word[5] = direction
	return word

ctrl= SpiController()
ctrl.configure('ftdi://ftdi:232h/1')  # Assuming there is only one FT232H.
spi = ctrl.get_port(cs=0, freq=100E3, mode=0)  # Assuming D3 is used for chip select.    
write_buf = b'\x75'


stepsize = 12
direction = 1

"""
while 1:
	print("MIN")
	word_m1 = set_word(0, 6*stepsize, 1 - direction)
	word_m2 = set_word(0, stepsize//2, direction)
	spi.write(word_m1 + word_m2, start=True, stop=True)
	time.sleep(5)

	print("MAX")
	word_m1 = set_word(171990 // 2, 6*stepsize, 1 - direction)
	word_m2 = set_word(171990 // 2, stepsize//2, direction)
	spi.write(word_m1 + word_m2, start=True, stop=True)
	time.sleep(5)
"""



"""
delay = 0
i = 0
N = 2
while 1:
	delay = (delay + 1) % 10
	if (delay == 0):
		i = (i + 1) % N
	target_phase_m1 = int(np.round((171990 / N) * i)) % 171990
	target_phase_m2 = 171990 - 1 - target_phase_m1

	target_phase_m1 = (target_phase_m1 + phase_m1_offset) % 171990
	target_phase_m2 = (target_phase_m2 + phase_m2_offset) % 171990

	word_m1 = set_word(target_phase_m1, 6*stepsize, 1 - direction)
	word_m2 = set_word(target_phase_m2, stepsize//2, direction)
	print("M1: Setting target phase " + str(target_phase_m1) + ", stepsize = " + str(stepsize) + ", direction = " + str(direction), ", SPI word = " + str(word_m1))
	print("M2: Setting target phase " + str(target_phase_m2) + ", stepsize = " + str(stepsize) + ", direction = " + str(direction), ", SPI word = " + str(word_m2))
	spi.write(word_m1 + word_m2, start=True, stop=True)
	time.sleep(.5)

"""

stepsize_fast = 48
stepsize_slow = 24

"""
MOTOR_IDX_MAIN = 0
MOTOR_IDX_AVG = 1

delay = 0
i = 0
N = 2
print("Calibrate axis. Please wait")
word_m1 = set_word(0, stepsize_fast, 1 - direction)
word_m2 = set_word(0, stepsize_fast, direction)
spi.write(word_m1 + word_m2, start=True, stop=True)
time.sleep(5)
prev_step = 0
step = 0
for motor_idx in [MOTOR_IDX_MAIN, MOTOR_IDX_AVG]:
	while 1:
		print("[MOTOR " + str(motor_idx) " + ] Enter the step (0 <= step < 171990) where the 1 ms point of the axis is. If the clock hand is aligned, just hit return.")
		prev_step = step
		s = input("> ")
		if s == "":
			break
		step = int(s) % 171990
		if step > prev_step:
			direction = 0
		else:
			direction = 1
		if motor_idx == MOTOR_IDX_MAIN:
			word_m1 = set_word(step, stepsize_fast, direction)
			step_m1 = step
		else:
			word_m2 = set_word(step, stepsize_fast, direction)
			step_m2 = step
		spi.write(word_m1 + word_m2, start=True, stop=True)
		time.sleep(1)

print("M1 calibration value: " + str(step_m1))
print("M2 calibration value: " + str(step_m2))
sys.exit(0)
"""

phase_m1_offset = 54000
phase_m2_offset = 2000



#read_1= spi.read(2, start=False, stop=True)

#id = spi.exchange([0x75],2).tobytes()
#print(read_1)
#print(id)

foo = .1
ping_ms = 1000.
prev_target_phase_m1 = 0
prev_target_phase_m2 = 0
avg_ping_ms = -1
hostname = "8.8.8.8"
hostname = "80.80.80.80"
#hostname = "192.168.88.1"
if True:
		while 1:
			ping_response = subprocess.Popen(["/bin/ping", "-c1", "-w1", hostname], stdout=subprocess.PIPE)
			ping_response.wait()
			ping_response.poll()
			if not (ping_response is None) \
			 and ping_response.returncode > 0:
				ping_ms = 10000.  # packet loss!
			else:
				ping_ms_str = ping_response.stdout.read()
				ping_ms_str = str(ping_ms_str)
				ping_ms_str = re.search("time=.+ms\\\\n\\\\n---", ping_ms_str).group()[5:-10]
				ping_ms = float(ping_ms_str)


			ping_ms = min(ping_ms, 10000)
			ping_ms = max(ping_ms, .1)
			if avg_ping_ms < 0:
				avg_hand_step = stepsize_fast
				avg_ping_ms = ping_ms
			else:
				avg_hand_step = 1

			avg_ping_ms = .9 * avg_ping_ms + .1 * ping_ms   # low-pass filtering

			if ping_ms > avg_ping_ms and math.log10(ping_ms / avg_ping_ms) > .04:
				avg_hand_step = stepsize_fast

			avg_ping_ms = .9 * max(ping_ms, avg_ping_ms) + .1 * ping_ms    # always track increasing ping fast

			import numpy as np
			import numpy.random
			foo *= 1.5
		#	ping_ms = foo
			""" + np.abs(np.random.normal(0, 500.)) + 1
			ping_ms = np.random.uniform(0, 4)
			ping_ms = 10**ping_ms + 1."""

			stepsize = 6
			target_phase_m1 = int(171990 // 2 * ping_to_angle(ping_ms)) + phase_m1_offset
			target_phase_m2 = int(171990 // 2 * ping_to_angle(avg_ping_ms)) + phase_m2_offset
			target_phase_m1 %= 171990
			target_phase_m2 %= 171990

			print("Current ping is: " + str(ping_ms) + " ms")
			m1_direction = 2
			m2_direction = 2
			word_m1 = set_word(target_phase_m1, stepsize_fast, m1_direction)
			word_m2 = set_word(target_phase_m2, avg_hand_step, m2_direction)
			print("M1: Setting target phase " + str(target_phase_m1) + ", stepsize = " + str(6*stepsize) + ", direction = " + str(m1_direction), ", SPI word = " + str(word_m1))
			print("M2: Setting target phase " + str(target_phase_m2) + ", stepsize = " + str(stepsize//2) + ", direction = " + str(m2_direction), ", SPI word = " + str(word_m2))
			spi.write(word_m1 + word_m2, start=True, stop=True)

			prev_target_phase_m1 = target_phase_m1
			prev_target_phase_m2 = target_phase_m2

			time.sleep(1)



