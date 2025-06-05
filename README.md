# Esp32OpenScaleSync
ESP32 BLE Scale -> Mqtt Gateway

Note: this is just a 1 hour PoC, the code is ugly and not cleaned up...

Right now only the SilverCrest SBF75 is implemented

Should work with any ESP32 that has support for BLE, e.g. WROOM32/C3/C6/S3 etc.

### Dev

use VsCode with PlatformIo

use serial output for debugging or attach a jtag debugger if you have one

Serial Log:

```
OpenScaleSync Booting...
Connecting to wifi... connected !
Synced rtc via ntp, time: 19:54:25
Connecting to BLE device... connected !
Registering with service
Service registered

Scale Initialization
Sending payload: E6 01 
Payload received: E6 00 20 
Received Init response
Updating scale time
Sending payload: E9 68 32 23 F2 

Requesting scale info
Sending payload: E7 4F 00 00 00 00 00 00 00 00 
Payload received: E7 F0 4F 01 55 14 14 01 01 01 01 05 
Received Scale info, battery: 85%

Requesting user list
Sending payload: E7 33 
Payload received: E7 F0 33 00 01 08 
Received user list count
Payload received: E7 34 01 01 00 00 00 00 00 00 00 65 59 4F 55 69 
Received user list data
Got username: YOU
All chunks received
Sending ack for chunk 1/1
Sending payload: E7 F1 34 01 01 

Requesting saved measurements
Sending payload: E7 41 00 00 00 00 00 00 00 65 
Payload received: E7 F0 41 02 00 
Received measurement count
Payload received: E7 XX XX XX XX XX XX XX XX XX XX XX XX XX XX 
Received measurement data
Sending ack for chunk 1/2
Sending payload: E7 F1 42 02 01 
Payload received: E7 XX XX XX XX XX XX XX XX XX XX XX XX XX XX 
Received measurement data
All chunks received
Sending ack for chunk 2/2
Sending payload: E7 F1 42 02 02 

Preparing mqtt payload...
Connecting to mqtt... connected !
Sending json payload via mqtt to openscalesync/measurement

Deleting saved measurements on scale
Sending payload: E7 43 00 00 00 00 00 00 00 65 
Payload received: E7 F0 43 00 
Received measurements deleted

Going to deep sleep, next wakeup in 8 hours
```


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