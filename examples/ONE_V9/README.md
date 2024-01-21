```bash
mkdir -p ./venv && \
  python3 -m venv ./venv && \
  . venv/bin/activate && \
  pip install pyserial
```

```bash
arduino-cli compile \
  --verbose \
  --fqbn esp32:esp32:lolin_c3_mini \
  --upload \
  --port /dev/ttyACM0 \
  --verify \
  ONE_V9.ino
```
