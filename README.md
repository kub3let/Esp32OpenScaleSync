# Esp32OpenScaleSync
ESP32 BLE Scale -> Mqtt Gateway

Note: this is just a 1 hour PoC, the code is ugly and not cleaned up...

Right now only the SilverCrest SBF75 is implemented

Should work with any ESP32 that has support for BLE, e.g. WROOM32/C3/C6/S3 etc.

### Todos
- [ ] cleanup code, refactor, create classes etc.
- [ ] optimize ble send/receive ack loop (async)
- [ ] implement more scale protocols

### Homeassistant

To track the values in homeassistant setup mqtt sensors like this:

```yaml
mqtt:
  sensor:
    - name: "OpenScale Battery"
      state_topic: "openscalesync/measurement"
      value_template: "{{ value_json.metadata.battery }}"
      unit_of_measurement: "%"

    - name: "OpenScale DateTime"
      state_topic: "openscalesync/measurement"
      value_template: "{{ value_json.measurements[0].timestamp | default(0) | timestamp_custom('%Y-%m-%d %H:%M:%S', True) }}"

    - name: "OpenScale Weight"
      state_topic: "openscalesync/measurement"
      value_template: "{{ value_json.measurements[0].weight }}"
      unit_of_measurement: "kg"

    - name: "OpenScale Fat Percentage"
      state_topic: "openscalesync/measurement"
      value_template: "{{ value_json.measurements[0].fat }}"
      unit_of_measurement: "%"

    - name: "OpenScale Water Percentage"
      state_topic: "openscalesync/measurement"
      value_template: "{{ value_json.measurements[0].water }}"
      unit_of_measurement: "%"

    - name: "OpenScale Muscle Percentage"
      state_topic: "openscalesync/measurement"
      value_template: "{{ value_json.measurements[0].muscle }}"
      unit_of_measurement: "%"

    - name: "OpenScale Bone Percentage"
      state_topic: "openscalesync/measurement"
      value_template: "{{ value_json.measurements[0].bone }}"
      unit_of_measurement: "%"

    - name: "OpenScale BMR"
      state_topic: "openscalesync/measurement"
      value_template: "{{ value_json.measurements[0].bmr }}"
      unit_of_measurement: "kcal"

    - name: "OpenScale AMR"
      state_topic: "openscalesync/measurement"
      value_template: "{{ value_json.measurements[0].amr }}"
      unit_of_measurement: "kcal"

    - name: "OpenScale BMI"
      state_topic: "openscalesync/measurement"
      value_template: "{{ value_json.measurements[0].bmi }}"
```