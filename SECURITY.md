# Security and Safety Policy

This project controls a mains-powered induction appliance. A software defect can
cause overheating, fire, electric shock, or damaged cookware.

## Report privately before publishing exploit details

For issues that could bypass physical Start, keep output active after Stop,
disable thermal/communication protection, disclose credentials, or expose the
local web UI, open a minimal GitHub security advisory after the repository is
published. Do not post working exploit steps in a public issue first.

## Safety boundaries

- Web endpoints must never control heat.
- Boot and reset must command Stop.
- Only the power task owns I²C.
- Do not expand the write-register whitelist.
- Do not add automatic restart after reset.
- No flash/eFuse procedure may be automated without an explicit release gate.
- Hardware testing must be supervised with suitable cookware/load and isolation.

The local web page is intended for a trusted home LAN only. Do not port-forward
it or expose it through a public reverse proxy.
