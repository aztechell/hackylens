---
contract-id: hackylens.capability.power
owner: platform-architecture
version: 0.1.0
stability: experimental
---

# Power service

Numeric ID `0x00010006`, canonical name `hackylens.cap.power`. The immutable
binding is selected at build time. `hk_power_service()` returns NULL on boards
without controlled power-off. `hk_power_request_off(handle, absolute_deadline)`
requires an explicit confirmation by its caller. It returns `HK_PENDING` while
shutdown preparation waits for runtime work to stop; repeat with the same deadline.
Preparation failures and deadline expiry prevent the hardware command. Successful
power removal may never return; `HK_OK` means the command was accepted.

The Cube provider uses AXP173. SEN0305 has no binding and excludes POWER OFF.
No key event calls this capability implicitly. The POWER OFF app requires a fresh
OK press on its confirmation screen; BACK cancels. Runtime shutdown preparation
stops MicroPython, flushes dirty settings and closes/unmounts userfs before the
PMIC command. A rejected command restores the filesystem mount.
