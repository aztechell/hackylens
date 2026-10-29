---
contract-id: hackylens.capability.power
owner: platform-architecture
version: 0.1.0
stability: experimental
---

# Power service

Numeric ID `0x00010006`, canonical name `hackylens.cap.power`. The immutable
binding is selected at build time. `hk_power_service()` returns NULL only when
no operations are supported. `hk_power_features()` reports actual OFF and RESTART
support; unsupported requests return `HK_ERR_CAPABILITY_ABSENT`.

`hk_power_request_off()` and `hk_power_request_restart()` take an absolute deadline.
Callers explicitly request each operation. `HK_PENDING` requires another call with
the same deadline. Both stop MicroPython, flush settings and unmount userfs before
any hardware command. Preparation errors or expiry prevent the command; returning
hardware failures restore the filesystem mount. Successful power removal/reset
normally never returns.

Cube supports OFF via AXP173 and RESTART via SoC soft reset. SEN supports RESTART
only, without a PMIC command. Restart does not use the watchdog recovery path.

The POWER app replaces the former SLEEP and POWER OFF menu items. LEFT/RIGHT selects
one of three horizontal icons. SLEEP enters the existing sleep session and wakes
on any new button press. One fresh OK press immediately executes the selected
action, including OFF/RESTART; there is no additional confirmation. An OK held
while entering POWER cannot execute an action until released and pressed again.
BACK from the selector closes it.
On SEN the OFF icon is disabled. Automatic sleep opens POWER directly in sleep
mode, preserving the existing inactivity behavior. POWER is not an autostart app;
the former SLEEP app had no persisted autostart ID.
