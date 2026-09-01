# Bluetooth Scanner ESP

An ESP-based edge Bluetooth scanner with HTTP reporting and a companion SensorHub dashboard.

## Repository layout

- `firmware/` — ESP scanner firmware.
- `sensorhub-server/` — Flask, SQLite, and Waitress receiver/dashboard for Windows or other Python hosts.

See [sensorhub-server/README.md](sensorhub-server/README.md) for server setup, API routes, and startup instructions.
