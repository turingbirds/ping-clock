/**
 * Ping clock motor driver code
 *
 * https://github.com/turingbirds/ping-clock
**/

// Look out! Enabling serial debug messages will cause the motion to stutter.
//#define SERIAL_DEBUG

#include <stdint.h>
#include <Arduino.h>

#include <math.h>
#include "driver/spi_slave.h"
#include "driver/spi_common.h"


/**
 *    emulate an SPI slave device
**/

#define RCV_HOST    HSPI_HOST

// #define GPIO_MISO 12
#define GPIO_MOSI 13
#define GPIO_SCLK 14
#define GPIO_CS 15

WORD_ALIGNED_ATTR char sendbuf[129]="";
WORD_ALIGNED_ATTR char recvbuf[129]="";

#define DIRECTION_CLOCKWISE 0
#define DIRECTION_COUNTERCLOCKWISE 1
#define DIRECTION_AUTO 2

#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))

volatile uint8_t delay_ = 0;

hw_timer_t * timer = NULL;

const int m1_pwm_a_pin = 19;
const int m1_en_a_pin = 18;
const int m1_pwm_b_pin = 17;
const int m1_en_b_pin = 5;
const int m1_pwm_c_pin = 16;
const int m1_en_c_pin = 32;

const int m2_pwm_a_pin = 33;
const int m2_en_a_pin = 4;
const int m2_pwm_b_pin = 25;
const int m2_en_b_pin = 2;
const int m2_pwm_c_pin = 27;
const int m2_en_c_pin = 26;

const int go_to_zero_pin = 21;

const int photo_interruptor_pin[2] = {35, 34};

const uint8_t MIN_STEPSIZE = 1;
const uint8_t CALIBRATION_STEPSIZE = 12;
const uint8_t MAX_STEPSIZE = 80;

#define SLOWEST_SPEED_TIMER_VAL (320000/(1<<(resolution-4)))
#define FASTEST_SPEED_TIMER_VAL (10000/(1<<(resolution-4)))


/**
 *  geometrical arrangement:
 *
 *  one full rotation consists of ``n_sectors`` sectors
 *  one sector consists of 6 BLDC driving phases
 *  one sector consists of ``max_duty`` steps
**/

const uint8_t n_motors = 2;
const uint8_t n_sectors = 7;   // 7 times through the sine table = 1 full rotation = 360°
const uint8_t n_bldc_phases = 6;
const uint8_t resolution = 12;
const uint16_t max_duty = (1 << resolution) - 1; // 12 bits
const uint32_t max_n_steps = max_duty + max_duty * ((n_bldc_phases-1) + n_bldc_phases * (n_sectors-1));

/**
 *  variables recording absolute position
**/

volatile uint8_t sector[n_motors]; // mod ``n_sectors``
volatile uint8_t bldc_phase[n_motors]; // mod ``n_bldc_phases``
volatile uint16_t phase[n_motors]; // mod ``max_duty``


/**
 *  variables for movement control
**/

volatile int8_t step[n_motors]; // velocity. negative for reverse direction
volatile uint32_t target_n_steps[n_motors];

uint32_t photo_interruptor_angle[n_motors]; // counted in phase ticks
uint8_t photo_interruptor_sector[n_motors];
uint8_t photo_interruptor_bldc_phase[n_motors];
uint16_t photo_interruptor_phase[n_motors];
volatile uint8_t prev_photo_interruptor_pin[2];

// setting PWM properties
const int freq = 25000; // pick this above auditory range! [Hz]
const int m1_pwm_a[n_motors] = {0, 3};
const int m1_pwm_b[n_motors] = {1, 4};
const int m1_pwm_c[n_motors] = {2, 5};


/**
 *  global modes of operation
**/

const uint8_t MODE_CALIBRATE_INIT = 1;
const uint8_t MODE_BALLISTIC_MOVEMENT = 2;
const uint8_t MODE_STOPPED = 0;

volatile uint8_t is_calibrated[n_motors];
volatile uint8_t mode[n_motors];
volatile uint16_t delay_counter = 0;


const float deg_single_sector = 360. / (float)n_sectors;
const float deg_single_bldc_phase = 360. / (float)n_sectors / (float)n_bldc_phases;


// called after a transaction is queued and ready for pickup by master
void my_post_setup_cb(spi_slave_transaction_t *trans) {
}

// called after transaction is sent/received
void my_post_trans_cb(spi_slave_transaction_t *trans) {
}


void init_spi_slave() {
  esp_err_t ret;

  //Configuration for the SPI bus
  spi_bus_config_t buscfg={
      .mosi_io_num=GPIO_MOSI,
      .miso_io_num=-1,
      .sclk_io_num=GPIO_SCLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
  };

  //Configuration for the SPI slave interface
  spi_slave_interface_config_t slvcfg;
      slvcfg.mode=0;
      slvcfg.spics_io_num=GPIO_CS;
      slvcfg.queue_size=3;
      slvcfg.flags=0;
      slvcfg.post_setup_cb=my_post_setup_cb;
      slvcfg.post_trans_cb=my_post_trans_cb;

  // Enable pull-ups on SPI lines so we don't detect rogue pulses when no master is connected.
  pinMode(GPIO_MOSI, INPUT_PULLUP);
  pinMode(GPIO_SCLK, INPUT_PULLUP);
  pinMode(GPIO_CS, INPUT_PULLUP);

  //Initialize SPI slave interface
  ret = spi_slave_initialize(RCV_HOST, &buscfg, &slvcfg, 0); // DMA disabled
  assert(ret == ESP_OK);
}


void IRAM_ATTR onTimer() {
  for (uint8_t motor_idx = 0; motor_idx < n_motors; ++motor_idx) {

    /**
     * movement state machine
    **/

    if (mode[motor_idx] == MODE_BALLISTIC_MOVEMENT) {
      if (target_n_steps[motor_idx] < abs(step[motor_idx])) {
        // target reached!
        step[motor_idx] = 0;
        mode[motor_idx] = MODE_STOPPED;
      }
      else {
        target_n_steps[motor_idx] -= abs(step[motor_idx]);
      }
    }

    /**
     * calibration state machine
    **/

    if (mode[motor_idx] == MODE_CALIBRATE_INIT) {
      target_n_steps[motor_idx] += abs(step[motor_idx]);
      const uint32_t new_photo_interruptor_pin_analog_value = analogRead(photo_interruptor_pin[motor_idx]);
      if (target_n_steps[motor_idx] > max_n_steps >> 2) { // this condition ensures at least some substantial rotation has happenend before concluding the calibration) {
        if (prev_photo_interruptor_pin[motor_idx] == 0
         && new_photo_interruptor_pin_analog_value > 512) {
          // low-to-high transition
          photo_interruptor_angle[motor_idx] += phase[motor_idx] + max_duty * (bldc_phase[motor_idx] + n_bldc_phases * sector[motor_idx]);
          ++is_calibrated[motor_idx];
        }
        else if (prev_photo_interruptor_pin[motor_idx] == 1
         && new_photo_interruptor_pin_analog_value <= 512) {
          // high-to-low transition
          photo_interruptor_angle[motor_idx] += phase[motor_idx] + max_duty * (bldc_phase[motor_idx] + n_bldc_phases * sector[motor_idx]);
          ++is_calibrated[motor_idx];
        }
      }

      if (is_calibrated[motor_idx] >= 2) {
        photo_interruptor_angle[motor_idx] >>= 1;  // take the average of the two summed terms
        step[motor_idx] = 0;
        mode[motor_idx] = MODE_STOPPED;
      }

      prev_photo_interruptor_pin[motor_idx] = new_photo_interruptor_pin_analog_value > 512 ? 1 : 0;
    }

    if (mode[motor_idx] == MODE_STOPPED) {
      continue;
    }

    /**
     * step through the space (sector, bldc_phase, phase)
    **/

    if (step[motor_idx] > 0 && phase[motor_idx] + step[motor_idx] >= max_duty) {
      phase[motor_idx] = phase[motor_idx] + step[motor_idx] - max_duty;
      if (bldc_phase[motor_idx] == n_bldc_phases - 1) {
        bldc_phase[motor_idx] = 0;
        ++sector[motor_idx];
        if (sector[motor_idx] >= n_sectors) {
          sector[motor_idx] = 0;
        }
      }
      else {
        ++bldc_phase[motor_idx];
      }
    }
    else if (step[motor_idx] < 0 && phase[motor_idx] < -step[motor_idx]) { // phase is pos, step is neg, e.g. phase = 24, step = -32 --> underflow!
      phase[motor_idx] = max_duty + step[motor_idx] + phase[motor_idx];
      if (bldc_phase[motor_idx] == 0) {
        bldc_phase[motor_idx] = n_bldc_phases - 1;
        if (sector[motor_idx] == 0) {
          sector[motor_idx] = n_sectors - 1;
        }
        else {
          --sector[motor_idx];
        }
      }
      else {
        --bldc_phase[motor_idx];
      }
    }
    else {
      phase[motor_idx] += step[motor_idx];
    }

    uint16_t dutycycle_1, dutycycle_2, dutycycle_3;

    if (bldc_phase[motor_idx] % 6 == 0) {
      dutycycle_1 = max_duty;
      dutycycle_2 = 0;
      dutycycle_3 = max_duty - phase[motor_idx];
    }
    else if (bldc_phase[motor_idx] % 6 == 1) {
      dutycycle_1 = max_duty;
      dutycycle_2 = phase[motor_idx];
      dutycycle_3 = 0;
    }
    else if (bldc_phase[motor_idx] % 6 == 2) {
      dutycycle_1 = max_duty - phase[motor_idx];
      dutycycle_2 = max_duty;
      dutycycle_3 = 0;
    }
    else if (bldc_phase[motor_idx] % 6 == 3) {
      dutycycle_1 = 0;
      dutycycle_2 = max_duty;
      dutycycle_3 = phase[motor_idx];
    }
    else if (bldc_phase[motor_idx] % 6 == 4) {
      dutycycle_1 = 0;
      dutycycle_2 = max_duty - phase[motor_idx];
      dutycycle_3 = max_duty;
    }
    else if (bldc_phase[motor_idx] % 6 == 5) {
      dutycycle_1 = phase[motor_idx];
      dutycycle_2 = 0;
      dutycycle_3 = max_duty;
    }

    dutycycle_1 = max(40, dutycycle_1);
    dutycycle_2 = max(40, dutycycle_2);
    dutycycle_3 = max(40, dutycycle_3);

    ledcWrite(m1_pwm_a[motor_idx], dutycycle_1);
    ledcWrite(m1_pwm_b[motor_idx], dutycycle_2);
    ledcWrite(m1_pwm_c[motor_idx], dutycycle_3);
  }
}


void start_calibration(size_t motor_idx) {
  is_calibrated[motor_idx] = 0;
  target_n_steps[motor_idx] = 0;
  step[motor_idx] = CALIBRATION_STEPSIZE;
  prev_photo_interruptor_pin[motor_idx] = analogRead(photo_interruptor_pin[motor_idx]) > 512 ? 1 : 0;
  photo_interruptor_angle[motor_idx] = 0;
  mode[motor_idx] = MODE_CALIBRATE_INIT;
}


void setup() {
  delay_ = 0;
  #ifdef SERIAL_DEBUG
  Serial.begin(115200);
  Serial.print("Begin setup()");
  #endif

  // configure LED PWM functionalitites
  ledcSetup(m1_pwm_a[0], freq, resolution);
  ledcSetup(m1_pwm_b[0], freq, resolution);
  ledcSetup(m1_pwm_c[0], freq, resolution);

  ledcSetup(m1_pwm_a[1], freq, resolution);
  ledcSetup(m1_pwm_b[1], freq, resolution);
  ledcSetup(m1_pwm_c[1], freq, resolution);

  // attach the channel to the GPIO to be controlled
  ledcAttachPin(m1_pwm_a_pin, m1_pwm_a[0]);
  ledcAttachPin(m1_pwm_b_pin, m1_pwm_b[0]);
  ledcAttachPin(m1_pwm_c_pin, m1_pwm_c[0]);

  ledcAttachPin(m2_pwm_a_pin, m1_pwm_a[1]);
  ledcAttachPin(m2_pwm_b_pin, m1_pwm_b[1]);
  ledcAttachPin(m2_pwm_c_pin, m1_pwm_c[1]);

  digitalWrite(m1_en_a_pin, LOW);
  pinMode(m1_en_a_pin, OUTPUT);
  digitalWrite(m1_en_a_pin, HIGH);

  digitalWrite(m1_en_b_pin, LOW);
  pinMode(m1_en_b_pin, OUTPUT);
  digitalWrite(m1_en_b_pin, HIGH);

  digitalWrite(m1_en_c_pin, LOW);
  pinMode(m1_en_c_pin, OUTPUT);
  digitalWrite(m1_en_c_pin, HIGH);

  digitalWrite(m2_en_a_pin, LOW);
  pinMode(m2_en_a_pin, OUTPUT);
  digitalWrite(m2_en_a_pin, HIGH);

  digitalWrite(m2_en_b_pin, LOW);
  pinMode(m2_en_b_pin, OUTPUT);
  digitalWrite(m2_en_b_pin, HIGH);

  digitalWrite(m2_en_c_pin, LOW);
  pinMode(m2_en_c_pin, OUTPUT);
  digitalWrite(m2_en_c_pin, HIGH);

  pinMode(photo_interruptor_pin[0], INPUT_PULLUP);
  pinMode(photo_interruptor_pin[1], INPUT_PULLUP);
  pinMode(go_to_zero_pin, INPUT_PULLUP);

  mode[0] = MODE_STOPPED;
  mode[1] = MODE_STOPPED;

  // slow start of the motors
  for (uint32_t i = 0; i < max_duty; i += 16) {
    for (uint8_t motor_idx = 0; motor_idx < n_motors; ++motor_idx) {
      ledcWrite(m1_pwm_a[motor_idx], i);
      ledcWrite(m1_pwm_c[motor_idx], i);
    }
    delay(1);
  }

  timer = timerBegin(0, 80, true); // 80 MHz / 80 = 1/us
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, SLOWEST_SPEED_TIMER_VAL, true); // timer trigger value; set auto-reload
  timerAlarmEnable(timer);

  delay(1000); // wait for initial transient after power on


  #ifdef SERIAL_DEBUG
  Serial.print("start_calibration()");
  #endif

  for (uint8_t motor_idx = 0; motor_idx < n_motors; ++motor_idx) {
    start_calibration(motor_idx);
  }

  #ifdef SERIAL_DEBUG
  Serial.print("init_spi_slave()");
  #endif

  init_spi_slave();
}


void move_to_step(uint8_t motor_idx, uint32_t target, uint8_t direction, unsigned int min_stepsize) {
  #ifdef SERIAL_DEBUG
  Serial.println();
  Serial.print("Move motor ");
  Serial.print(motor_idx);
  Serial.print(" to target step: ");
  Serial.println(target);
  #endif

  target = (max_n_steps + target + photo_interruptor_angle[motor_idx]) % max_n_steps;

  // stop the motor as late as possible (before reading ``current_step``)
  mode[motor_idx] = MODE_STOPPED;

  const uint32_t current_step = phase[motor_idx] + max_duty * (bldc_phase[motor_idx] + n_bldc_phases * sector[motor_idx]);

  if (direction == DIRECTION_AUTO) {
    if (current_step > target) {
      if (current_step > target + (max_n_steps>>1)) {
        direction = DIRECTION_CLOCKWISE;
      }
      else {
        direction = DIRECTION_COUNTERCLOCKWISE;
      }
    }
    else {
      // target > current_step
      if (target > current_step + (max_n_steps>>1)) {
        direction = DIRECTION_COUNTERCLOCKWISE;
      }
      else {
        direction = DIRECTION_CLOCKWISE;
      }
    }
  }
  if (direction == DIRECTION_CLOCKWISE) {
    if (target > current_step) {
      target_n_steps[motor_idx] = target - current_step;
    }
    else {
      if (current_step - target < min_stepsize) {
        target_n_steps[motor_idx] = 0;
      }
      else {
        target_n_steps[motor_idx] = max_n_steps + target - current_step;
      }
    }
  }
  else if (direction == DIRECTION_COUNTERCLOCKWISE) {
    if (target < current_step) {
      target_n_steps[motor_idx] = current_step - target;
    }
    else {
      // target >= current_step
      if (target - current_step < min_stepsize) {
        target_n_steps[motor_idx] = 0;
      }
      else {
        target_n_steps[motor_idx] = max_n_steps + current_step - target;
      }
    }
  }

  if (direction == DIRECTION_COUNTERCLOCKWISE) {
    step[motor_idx] = -min_stepsize;
  }
  else {
    step[motor_idx] = min_stepsize;
  }

  #ifdef SERIAL_DEBUG
  Serial.print(", current step: ");
  Serial.print(current_step);
  Serial.print(", max_n_steps: ");
  Serial.print(max_n_steps);
  Serial.print(", calibration step: ");
  Serial.print(photo_interruptor_angle[motor_idx]);
  Serial.print(", direction: ");
  Serial.print(direction);
  Serial.print(", stepping by n_steps: ");
  Serial.print(target_n_steps[motor_idx]);
  Serial.print(", One step is: ");
  Serial.print(step[motor_idx]);
  Serial.println();
  #endif

  mode[motor_idx] = MODE_BALLISTIC_MOVEMENT;
}


void loop() {

#ifdef SERIAL_DEBUG
  for (uint8_t motor_idx = 0; motor_idx < n_motors; ++motor_idx) {
    Serial.print("Motor ");
    Serial.print(motor_idx);
    Serial.print(": phase = ");
    Serial.print(phase[motor_idx] + max_duty * (bldc_phase[motor_idx] + n_bldc_phases * sector[motor_idx]));
    Serial.print(": mode = ");
    Serial.print(mode[motor_idx]);
    Serial.println();
  }
#endif

  delay(100);

  if (!digitalRead(go_to_zero_pin) && mode[0] == MODE_STOPPED && mode[1] == MODE_STOPPED) {
    #ifdef SERIAL_DEBUG
    Serial.println("Go to zero asserted and motors are stopped.");
    #endif
    move_to_step(0, 0, DIRECTION_CLOCKWISE, CALIBRATION_STEPSIZE);
    move_to_step(1, 0, DIRECTION_COUNTERCLOCKWISE, CALIBRATION_STEPSIZE);
    return;
  }

  if (mode[0] == MODE_CALIBRATE_INIT || mode[1] == MODE_CALIBRATE_INIT) {
    // still calibrating
    return;
  }

  sprintf(sendbuf, "This is the receiver, sending data for transmission number %04d.", delay_);
  spi_slave_free(RCV_HOST);
  init_spi_slave();
  spi_slave_transaction_t t;
  memset(&t, 0, sizeof(t));
  memset(recvbuf, 0xA5, 8);

  // set up transaction
  t.length=12*8;
  t.tx_buffer=sendbuf;
  t.rx_buffer=recvbuf;

  esp_err_t ret = spi_slave_transmit(RCV_HOST, &t, portMAX_DELAY);

  // spi_slave_transmit does not return until the master has done a transmission, so by here we have sent our data and received data from the master
  #ifdef SERIAL_DEBUG
  Serial.print("SPI transfer return code: ");
  Serial.print(ret);
  Serial.print(", received data: ");
  for (uint8_t foo = 0; foo < 12; ++foo) {
    Serial.printf("%d ", recvbuf[foo]);
  }
  Serial.println();
  #endif

  if (ret == 0) {
    uint32_t target_phase_m1 = recvbuf[0] + (recvbuf[1] << 8) + (recvbuf[2] << 16) + (recvbuf[3] << 24);
    uint8_t stepsize_m1 = recvbuf[4];
    uint8_t direction_m1 = recvbuf[5];

    #ifdef SERIAL_DEBUG
    Serial.print("SPI command: m1 moving to phase ");
    Serial.print(target_phase_m1);
    Serial.print(", stepsize = ");
    Serial.print(stepsize_m1);
    Serial.print(", direction = ");
    Serial.print(direction_m1);
    Serial.println();
    #endif

    uint32_t target_phase_m2 = recvbuf[0 + 6] + (recvbuf[1 + 6] << 8) + (recvbuf[2 + 6] << 16) + (recvbuf[3 + 6] << 24);
    uint8_t stepsize_m2 = recvbuf[4 + 6];
    uint8_t direction_m2 = recvbuf[5 + 6];
    #ifdef SERIAL_DEBUG
    Serial.print("SPI command: m2 moving to phase ");
    Serial.print(target_phase_m2);
    Serial.print(", stepsize = ");
    Serial.print(stepsize_m2);
    Serial.print(", direction = ");
    Serial.print(direction_m2);
    Serial.println();
    #endif

    target_phase_m1 = min(target_phase_m1, max_n_steps);
    target_phase_m2 = min(target_phase_m2, max_n_steps);

    move_to_step(0, target_phase_m1, direction_m1, stepsize_m1);
    move_to_step(1, target_phase_m2, direction_m2, stepsize_m2);
  }


  // // autonomous movement demo
  // ++delay_;
  // if (delay_ > 5 && delay_ <= 10) {
  //   move_to_step(0, 0, DIRECTION_CLOCKWISE, 4*CALIBRATION_STEPSIZE);
  //   move_to_step(1, 0, DIRECTION_CLOCKWISE, 4*CALIBRATION_STEPSIZE); // slowest possible!
  //   Serial.println("0");
  // }
  // else if (delay_ > 10 && delay_ <= 15) {
  //   move_to_step(0, max_n_steps >> 2, DIRECTION_CLOCKWISE, 2*CALIBRATION_STEPSIZE);
  //   move_to_step(1, max_n_steps >> 2, DIRECTION_CLOCKWISE, CALIBRATION_STEPSIZE); // slowest possible!
  //   Serial.println("90");
  // }
  // else if (delay_ > 15 && delay_ <= 20) {
  //   move_to_step(0, max_n_steps >> 1, DIRECTION_CLOCKWISE, 2*CALIBRATION_STEPSIZE);
  //   move_to_step(1, max_n_steps >> 1, DIRECTION_CLOCKWISE, CALIBRATION_STEPSIZE); // slowest possible!
  //   Serial.println("180");
  // }
  // else if (delay_ > 25) {
  //   move_to_step(0, (max_n_steps >> 2) + (max_n_steps >> 1), DIRECTION_CLOCKWISE, 2*CALIBRATION_STEPSIZE);
  //   move_to_step(1, (max_n_steps >> 2) + (max_n_steps >> 1), DIRECTION_CLOCKWISE, CALIBRATION_STEPSIZE); // slowest possible!
  //   Serial.println("270");
  //   delay_ = 0;
  // }
}
