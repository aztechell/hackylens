# HackyLens

HackyLens 1.0.0 full modular firmware.

This repo is a restart from the known-working v58 firmware. The current tree keeps v58 behavior intact, but the firmware is now built from real C translation units instead of a `full.c` include-chain. Apps are registered through a compile-time app registry, and simple apps can be removed from the build with `--disable-app`.

Default apps: TERMINAL, CAMERA, QR-CAMERA, FACE DETECT, FILES, BUTTONS, PONG, SETTINGS, and SLEEP.

## Build

```powershell
python hackylens\tools\check_env.py
python hackylens\tools\check_arch.py
python hackylens\tools\build_firmware.py full
python hackylens\tools\build_firmware.py full --disable-app buttons
```

Default output:

```text
hackylens\build\hackylens.bin
```

## Flash

```powershell
python hackylens\tools\hkflash.py flash hackylens\build\hackylens.bin --port COM10
python hackylens\tools\hkflash.py monitor --port COM10 --reset-before-read --duration 10
```

## License

HackyLens is released under the [MIT License](LICENSE).
