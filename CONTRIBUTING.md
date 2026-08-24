# Contributing

Read `docs/AI_DEVELOPMENT_CONTEXT.md` before changing control code.

## Pull-request checklist

1. Keep heat Start/Stop/Pause and live setpoint control on the physical panel.
2. Preserve safe boot Stop, one I²C owner, 500 ms heartbeat, write whitelist, and
   watchdog behavior.
3. Do not commit dumps, NVS, captures, credentials, personal paths, local IPs, or
   unit identifiers.
4. Build the production app and run:

   ```powershell
   python firmware\production\tests\safety_check.py
   python firmware\production\tests\localization_check.py
   python tools\validate_manual.py
   python tools\public_release_audit.py
   ```

5. New UI strings must fit the 64×48 OLED in English, Russian, and Simplified
   Chinese without scrolling.
6. New hardware behavior needs evidence: logic capture, scope measurement,
   datasheet, or controlled test log. Label assumptions clearly.
7. A successful build is not authorization to flash or energize hardware.
