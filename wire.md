/****************************************************
 *  GO TIME NECKLACE — ESP32-C3 PIN MAP + WIRING NOTES
 *  - 1 vibration motor (via NPN transistor)
 *  - 1 touch sensor
 *  - 1 microwave radar sensor
 *  - 1 IMU (I2C)
 ****************************************************/

// =====================
//  VIBRATION MOTOR STAGE
// =====================
// Motor is NOT driven directly from ESP32-C3.
// Use an NPN transistor (e.g., 2N2222, S8050) or MOSFET.
// Recommended supply: 3.3–5V depending on motor.
//
// WIRING:
//  - ESP32-C3 GPIO → base (via resistor)
//  - Motor + → external V+ (battery or 5V)
//  - Motor – → transistor collector/drain
//  - Transistor emitter/source → GND
//  - Flyback diode across motor (1N4148 / 1N5819):
//      Anode to motor –, cathode to motor +
//  - Optional capacitor (100µF) across motor supply for smoothing.
//
// COMPONENTS:
//  - Base resistor: 1k–4.7kΩ (start with 2.2kΩ)
//  - Flyback diode: 1N4148 or 1N5819
//  - Bulk capacitor: 100µF electrolytic across V+ and GND near motor

#define PIN_VIBE        5      // GPIO driving transistor base via resistor
// Example:
//  GPIO5 → 2.2kΩ resistor → transistor base
//  Motor + → 5V
//  Motor – → transistor collector
//  Transistor emitter → GND
//  Diode: anode at motor –, cathode at motor +


// =====================
//  TOUCH SENSOR
// =====================
// Can be:
//  - Capacitive touch pad
//  - Simple metal pad with touch IC
//
// If using direct GPIO touch:
//  - Enable internal pull-up
//  - Optional small capacitor (10–100nF) to GND for noise filtering.

#define PIN_TOUCH       6      // Touch input for mode switching


// =====================
//  RADAR SENSOR
// =====================
// RCWL-0516 (digital):
//  - OUT pin → GPIO (digital input)
//  - VCC → 5V
//  - GND → common GND
//  - Optional 10µF capacitor across VCC–GND near module.
//
// CDM324/HB100 (analog via amplifier):
//  - Amplifier output → ADC-capable GPIO
//  - VCC → 5V or 3.3V per module spec
//  - GND → common GND
//  - Decoupling capacitor: 100nF close to module between VCC–GND.

#define PIN_RADAR       7      // Radar signal input (ADC or digital)


// =====================
//  IMU (I2C)
// =====================
// MPU6050 / MPU6886 / QMI8658 etc.
//  - VCC → 3.3V
//  - GND → common GND
//  - SCL → GPIO8
//  - SDA → GPIO9
//  - Pull-up resistors: 4.7kΩ to 3.3V on SCL and SDA (if not on breakout)
//  - Decoupling capacitor: 100nF near IMU between VCC–GND.

#define PIN_IMU_SCL     8      // I2C SCL
#define PIN_IMU_SDA     9      // I2C SDA


// =====================
//  STATUS LED (optional)
// =====================
// Simple debug LED:
//  - LED anode → GPIO via 330–1kΩ resistor
//  - LED cathode → GND

#define PIN_STATUS_LED  10     // Optional indicator LED


// =====================
//  POWER + GROUND NOTES
// =====================
// - Tie ALL grounds together: ESP32-C3 GND, motor GND, radar GND, IMU GND.
// - Keep motor wiring physically away from IMU and radar where possible.
// - Put decoupling caps:
//     * 100nF near ESP32-C3 VCC
//     * 100nF near IMU VCC
//     * 10–100µF near motor supply.
// - If noise causes false triggers, increase motor supply capacitance
//   and add small caps (10–100nF) on sensor VCC lines.
