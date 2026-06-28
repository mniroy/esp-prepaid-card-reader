# Wiring Guide: GP2Y1014AU to Wemos D1 Mini

**Important Note:** This sensor requires an external resistor and capacitor (usually included with the sensor) to function properly and stabilize the power supply for the internal infrared LED.

**Required Components:**
1. 1x Resistor **150 Ohm**
2. 1x Capacitor **220µF** (Electrolytic, pay attention to polarity)

## Pin Connections

| Sensor Pin (Common Color) | Function | Connection on Wemos D1 Mini / Components |
| :--- | :--- | :--- |
| **1 (Blue)** | V-LED | Connect to **150Ω Resistor**, then to **5V** on Wemos.<br>*(Also connect the **Positive (+)** leg of the 220µF Capacitor here).* |
| **2 (Green)** | LED-GND | Connect to **GND** (G pin on Wemos).<br>*(Also connect the **Negative (-)** leg of the 220µF Capacitor here).* |
| **3 (White)** | LED-IN | Connect to Pin **D3** (GPIO 0). This controls the internal LED. |
| **4 (Yellow)** | S-GND | Connect to **GND** (G pin on Wemos). |
| **5 (Black)** | S-OUT | Connect to Pin **A0** (Analog Input on Wemos). |
| **6 (Red)** | VCC | Connect directly to **5V** (5V pin on Wemos). |

## Resistor and Capacitor Circuit Diagram
This circuit is mandatory to stabilize the current drawn by the IR LED when it pulses:
- **150Ω Resistor**: Placed in series between the **5V** source and sensor pin **1 (Blue)**.
- **220µF Capacitor**: Placed in parallel:
  - Positive (long) leg to pin **1 (Blue)** (after the resistor).
  - Negative (short/striped) leg to pin **2 (Green)** or **GND**.

> **Soldering & Voltage Tips:**
> - The `5V` pin on the Wemos D1 Mini is connected directly to the USB power, providing sufficient current for this sensor. Make sure to solder to 5V, not 3.3V, as the sensor operates optimally at 5V.
> - The `A0` pin on the Wemos D1 Mini has a built-in voltage divider allowing it to safely read up to 3.3V. The max output of the GP2Y1014AU is generally around 3.5V under extremely heavy dust conditions, making it safe to connect directly to A0 without additional voltage dividers.
