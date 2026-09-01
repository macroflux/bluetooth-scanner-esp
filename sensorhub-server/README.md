# SensorHub Server

SensorHub is the companion receiver and dashboard for the ESP Bluetooth scanner. It uses Flask, SQLite, Waitress, and plain HTML/CSS/JavaScript.

## Features

- Receives Bluetooth Classic and BLE scan batches over HTTP.
- Stores scan data locally in SQLite.
- Shows scan totals, recent volume, BLE signal proximity, latest observations, and reports.
- Provides Classic and BLE observation tables with JSON drilldown.
- Refreshes the dashboard every 15 seconds.
- Requires no Node.js build tools or external chart library.

The BLE proximity display uses RSSI as relative signal strength. Device angles are deterministic visual placements only; they do not represent direction, bearing, or physical location.

## Requirements

- Python 3.7 or later
- Network access between the scanner and the server

## Windows setup

Open Command Prompt in this folder and run:

```bat
py -m pip install -r requirements.txt
run_sensorhub.bat
```

The launcher resolves its own directory, so the repository can be placed anywhere on the computer.

Open the dashboard locally at:

```text
http://127.0.0.1:8000/
```

From another device, replace `<server-address>` with the server computer's hostname or LAN address:

```text
http://<server-address>:8000/
```

## Scanner configuration

Set the firmware ingestion URL to the address of the computer running SensorHub:

```cpp
const char *API_URL =
  "http://<server-address>:8000/api/v1/bluetooth/scans";
```

## Database location

By default, SensorHub creates `sensorhub.sqlite3` beside `app.py`. Database files are excluded from Git to prevent collected observations from being committed.

To store the database elsewhere, set `SENSORHUB_DB` before starting the service:

```bat
set SENSORHUB_DB=D:\path\to\sensorhub.sqlite3
run_sensorhub.bat
```

## Manual startup

Windows:

```bat
py -m waitress --threads=8 --listen=0.0.0.0:8000 app:app
```

Linux or macOS:

```sh
python3 -m waitress --threads=8 --listen=0.0.0.0:8000 app:app
```

## API routes

| Method | Route | Purpose |
| --- | --- | --- |
| `GET` | `/api/v1/health` | Service and database health |
| `GET` | `/api/v1/endpoints` | Endpoint discovery |
| `GET`, `POST` | `/api/v1/bluetooth/scans` | List or ingest Bluetooth scan batches |
| `GET` | `/api/v1/bluetooth/scans/<id>` | Retrieve one scan batch and its observations |
| `GET`, `POST` | `/api/v1/telemetry/observations` | List or ingest generic telemetry |
| `GET` | `/api/v1/dashboard/summary` | Dashboard totals and recent scan timeline |
| `GET` | `/api/v1/dashboard/recent` | Recent Classic and BLE observations |
| `GET` | `/api/v1/reports/bluetooth` | Scanner and BLE service reports |

## Updating an existing installation

Copy the application files into the existing server folder, but keep its current `sensorhub.sqlite3`. SensorHub creates missing tables and compatible columns at startup without removing stored scans.
