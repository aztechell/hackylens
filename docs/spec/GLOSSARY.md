# Terms

- **Board / BSP:** one physical product and its wiring, descriptor, startup and
  electrical preparation. Apps do not infer hardware from board IDs.
- **Platform / HAL:** processor-family implementation and bounded peripheral
  primitives. K210 SDK details stay here rather than in apps.
- **Driver:** implementation for a device or peripheral. It does not own app
  workflows.
- **Service:** reusable behavior with explicit lifetimes, errors and cleanup.
  Only service headers intentionally published by the SDK are external APIs.
- **Capability:** the historical directory name for the public typed Time,
  Input, Lights, Display and External Link services; it does not imply a broker.
- **Binding:** immutable selected implementation of a typed service.
- **Session:** stable caller-owned storage for a resource claim. Open sessions
  cannot be copied or moved. Conflicts and quarantine are service-local.
- **Borrow:** temporary access to an existing buffer or workspace. Its lifetime
  ends at the documented release or terminal operation; it is not an allocation.
- **Runtime:** foreground app lifecycle, ordered dispatch, bounded state and
  resource cleanup. Context generations reject stale app work.
- **App:** statically composed feature with its own state and policy. Bundled
  apps may use permitted portable firmware services; standalone apps use the SDK.
- **Adapter:** maps a service to another client surface, such as MicroPython,
  preserving resource lifetimes and deadlines without duplicating hardware logic.
- **Experimental:** a usable interface that can evolve through documented
  version changes; explicit compatibility requirements still apply.

In specifications, MUST and MUST NOT describe required behavior; SHOULD describes
a default with a reasoned exception; MAY describes optional behavior.
