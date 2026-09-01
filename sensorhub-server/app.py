from __future__ import annotations
import json, os, sqlite3
from datetime import datetime, timezone
from pathlib import Path
from flask import Flask, abort, g, jsonify, render_template, request

BASE_DIR = Path(__file__).resolve().parent
DB_PATH = Path(os.getenv('SENSORHUB_DB', BASE_DIR / 'sensorhub.sqlite3'))
app = Flask(__name__)
app.config['JSON_SORT_KEYS'] = False

def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec='milliseconds').replace('+00:00','Z')

def get_db():
    if 'db' not in g:
        db = sqlite3.connect(DB_PATH)
        db.row_factory = sqlite3.Row
        db.execute('PRAGMA foreign_keys=ON')
        db.execute('PRAGMA journal_mode=WAL')
        g.db = db
    return g.db

@app.teardown_appcontext
def close_db(_exc=None):
    db = g.pop('db', None)
    if db is not None: db.close()

def init_db():
    db = sqlite3.connect(DB_PATH)
    db.execute('PRAGMA foreign_keys=ON')
    db.executescript('''
    CREATE TABLE IF NOT EXISTS scan_batches (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      scanner_id TEXT NOT NULL, device_scan_id INTEGER, received_at TEXT NOT NULL,
      scan_started_uptime_ms INTEGER, scan_finished_uptime_ms INTEGER,
      classic_count INTEGER NOT NULL DEFAULT 0, ble_count INTEGER NOT NULL DEFAULT 0,
      source_ip TEXT, raw_json TEXT NOT NULL
    );
    CREATE TABLE IF NOT EXISTS classic_observations (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      scan_batch_id INTEGER NOT NULL REFERENCES scan_batches(id) ON DELETE CASCADE,
      address TEXT, name TEXT, name_lookup_attempted INTEGER, name_lookup_succeeded INTEGER,
      class_raw TEXT, class_value INTEGER, class_format_type INTEGER,
      class_major_code INTEGER, class_major TEXT, class_minor_code INTEGER, class_minor TEXT,
      service_class_bits INTEGER, services_json TEXT, rssi_raw TEXT, rssi_dbm INTEGER, raw_json TEXT
    );
    CREATE TABLE IF NOT EXISTS ble_observations (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      scan_batch_id INTEGER NOT NULL REFERENCES scan_batches(id) ON DELETE CASCADE,
      address TEXT, address_type_raw INTEGER, rssi INTEGER, name TEXT, tx_power INTEGER,
      appearance INTEGER, manufacturer_data_hex TEXT, service_uuid TEXT,
      service_data_hex TEXT, service_data_uuid TEXT, raw_payload_hex TEXT, raw_json TEXT
    );
    CREATE TABLE IF NOT EXISTS telemetry_observations (
      id INTEGER PRIMARY KEY AUTOINCREMENT, endpoint TEXT NOT NULL, device_id TEXT,
      received_at TEXT NOT NULL, source_ip TEXT, payload_json TEXT NOT NULL
    );
    ''')
    db.commit(); db.close()

def ensure_columns():
    db = sqlite3.connect(DB_PATH); db.row_factory = sqlite3.Row
    def cols(t): return {r['name'] for r in db.execute(f'PRAGMA table_info({t})')}
    classic = {'name_lookup_attempted':'INTEGER','name_lookup_succeeded':'INTEGER','class_format_type':'INTEGER','class_major_code':'INTEGER','class_major':'TEXT','class_minor_code':'INTEGER','class_minor':'TEXT','service_class_bits':'INTEGER','services_json':'TEXT','raw_json':'TEXT'}
    ble = {'raw_json':'TEXT'}
    existing = cols('classic_observations')
    for n,t in classic.items():
        if n not in existing: db.execute(f'ALTER TABLE classic_observations ADD COLUMN {n} {t}')
    existing = cols('ble_observations')
    for n,t in ble.items():
        if n not in existing: db.execute(f'ALTER TABLE ble_observations ADD COLUMN {n} {t}')
    db.commit(); db.close()

def require_json():
    if not request.is_json: abort(415, description='Content-Type must be application/json')
    p = request.get_json()
    if not isinstance(p, dict): abort(400, description='JSON body must be an object')
    return p

@app.get('/')
def dashboard(): return render_template('index.html')

@app.get('/api/v1/health')
def health():
    n = get_db().execute('SELECT COUNT(*) n FROM scan_batches').fetchone()['n']
    return jsonify(status='ok', service='sensorhub', version='0.2.0', utc=utc_now(), scan_batches=n)

@app.get('/api/v1/endpoints')
def endpoints():
    return jsonify(endpoints=[
      {'name':'Bluetooth scan batches','path':'/api/v1/bluetooth/scans','methods':['GET','POST']},
      {'name':'Bluetooth report','path':'/api/v1/reports/bluetooth','methods':['GET']},
      {'name':'Generic telemetry','path':'/api/v1/telemetry/observations','methods':['GET','POST']},
      {'name':'Health','path':'/api/v1/health','methods':['GET']}
    ])

@app.post('/api/v1/bluetooth/scans')
def create_scan():
    p=require_json(); scanner=str(p.get('scanner_id') or '').strip()
    if not scanner: abort(400, description='scanner_id is required')
    classic, ble = p.get('classic',[]), p.get('ble',[])
    if not isinstance(classic,list) or not isinstance(ble,list): abort(400, description='classic and ble must be arrays')
    db=get_db(); now=utc_now()
    cur=db.execute('''INSERT INTO scan_batches(scanner_id,device_scan_id,received_at,scan_started_uptime_ms,scan_finished_uptime_ms,classic_count,ble_count,source_ip,raw_json) VALUES(?,?,?,?,?,?,?,?,?)''',
      (scanner,p.get('scan_id'),now,p.get('scan_started_uptime_ms'),p.get('scan_finished_uptime_ms'),len(classic),len(ble),request.remote_addr,json.dumps(p,separators=(',',':'))))
    bid=cur.lastrowid
    for i in classic:
        if not isinstance(i,dict): continue
        db.execute('''INSERT INTO classic_observations(scan_batch_id,address,name,name_lookup_attempted,name_lookup_succeeded,class_raw,class_value,class_format_type,class_major_code,class_major,class_minor_code,class_minor,service_class_bits,services_json,rssi_raw,rssi_dbm,raw_json) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)''',
          (bid,i.get('address'),i.get('name'),i.get('name_lookup_attempted'),i.get('name_lookup_succeeded'),i.get('class_raw'),i.get('class_value'),i.get('class_format_type'),i.get('class_major_code'),i.get('class_major'),i.get('class_minor_code'),i.get('class_minor'),i.get('service_class_bits'),json.dumps(i.get('services',[])),i.get('rssi_raw'),i.get('rssi_dbm'),json.dumps(i,separators=(',',':'))))
    for i in ble:
        if not isinstance(i,dict): continue
        db.execute('''INSERT INTO ble_observations(scan_batch_id,address,address_type_raw,rssi,name,tx_power,appearance,manufacturer_data_hex,service_uuid,service_data_hex,service_data_uuid,raw_payload_hex,raw_json) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)''',
          (bid,i.get('address'),i.get('address_type_raw'),i.get('rssi'),i.get('name'),i.get('tx_power'),i.get('appearance'),i.get('manufacturer_data_hex'),i.get('service_uuid'),i.get('service_data_hex'),i.get('service_data_uuid'),i.get('raw_payload_hex'),json.dumps(i,separators=(',',':'))))
    db.commit()
    return jsonify(ok=True,scan_batch_id=bid,scanner_id=scanner,received_at=now,classic_stored=len(classic),ble_stored=len(ble)),201

@app.get('/api/v1/bluetooth/scans')
def list_scans():
    limit=min(max(request.args.get('limit',50,type=int),1),500)
    rows=get_db().execute('SELECT id,scanner_id,device_scan_id,received_at,scan_started_uptime_ms,scan_finished_uptime_ms,classic_count,ble_count,source_ip FROM scan_batches ORDER BY id DESC LIMIT ?',(limit,)).fetchall()
    return jsonify([dict(r) for r in rows])

@app.get('/api/v1/bluetooth/scans/<int:batch_id>')
def scan_detail(batch_id):
    db=get_db(); b=db.execute('SELECT * FROM scan_batches WHERE id=?',(batch_id,)).fetchone()
    if not b: abort(404, description='scan batch not found')
    c=db.execute('SELECT * FROM classic_observations WHERE scan_batch_id=? ORDER BY id',(batch_id,)).fetchall()
    le=db.execute('SELECT * FROM ble_observations WHERE scan_batch_id=? ORDER BY id',(batch_id,)).fetchall()
    return jsonify(batch=dict(b),classic=[dict(r) for r in c],ble=[dict(r) for r in le])

@app.post('/api/v1/telemetry/observations')
def create_telemetry():
    p=require_json(); now=utc_now(); db=get_db()
    cur=db.execute('INSERT INTO telemetry_observations(endpoint,device_id,received_at,source_ip,payload_json) VALUES(?,?,?,?,?)',(str(p.get('type') or 'generic'),p.get('device_id'),now,request.remote_addr,json.dumps(p,separators=(',',':'))))
    db.commit(); return jsonify(ok=True,observation_id=cur.lastrowid,received_at=now),201

@app.get('/api/v1/telemetry/observations')
def list_telemetry():
    rows=get_db().execute('SELECT * FROM telemetry_observations ORDER BY id DESC LIMIT 100').fetchall()
    return jsonify([dict(r) for r in rows])

@app.get('/api/v1/dashboard/summary')
def summary():
    db=get_db()
    totals=db.execute('SELECT COUNT(*) scans,COALESCE(SUM(classic_count),0) classic_observations,COALESCE(SUM(ble_count),0) ble_observations,COUNT(DISTINCT scanner_id) scanners FROM scan_batches').fetchone()
    uble=db.execute('SELECT COUNT(DISTINCT address) n FROM ble_observations').fetchone()['n']
    uclassic=db.execute('SELECT COUNT(DISTINCT address) n FROM classic_observations').fetchone()['n']
    latest=db.execute('SELECT id,scanner_id,device_scan_id,received_at,classic_count,ble_count,source_ip FROM scan_batches ORDER BY id DESC LIMIT 1').fetchone()
    timeline=db.execute('SELECT id,scanner_id,received_at,classic_count,ble_count FROM scan_batches ORDER BY id DESC LIMIT 30').fetchall()
    top=db.execute('''SELECT COALESCE(NULLIF(name,''),address,'(unknown)') label,address,COUNT(*) sightings,ROUND(AVG(rssi),1) avg_rssi,MAX(rssi) strongest_rssi FROM ble_observations GROUP BY address ORDER BY sightings DESC,strongest_rssi DESC LIMIT 12''').fetchall()
    return jsonify(totals=dict(totals),unique={'ble_addresses':uble,'classic_addresses':uclassic},latest=dict(latest) if latest else None,timeline=[dict(r) for r in reversed(timeline)],top_ble=[dict(r) for r in top])

@app.get('/api/v1/dashboard/recent')
def recent():
    db=get_db()
    c=db.execute('SELECT c.*,s.scanner_id,s.received_at FROM classic_observations c JOIN scan_batches s ON s.id=c.scan_batch_id ORDER BY c.id DESC LIMIT 30').fetchall()
    b=db.execute('SELECT b.*,s.scanner_id,s.received_at FROM ble_observations b JOIN scan_batches s ON s.id=b.scan_batch_id ORDER BY b.id DESC LIMIT 50').fetchall()
    return jsonify(classic=[dict(r) for r in c],ble=[dict(r) for r in b])

@app.get('/api/v1/reports/bluetooth')
def report():
    db=get_db()
    scanners=db.execute('SELECT scanner_id,COUNT(*) scans,SUM(classic_count) classic_observations,SUM(ble_count) ble_observations,MIN(received_at) first_seen,MAX(received_at) last_seen FROM scan_batches GROUP BY scanner_id ORDER BY last_seen DESC').fetchall()
    services=db.execute("SELECT COALESCE(service_uuid,service_data_uuid,'(none)') service,COUNT(*) observations FROM ble_observations GROUP BY COALESCE(service_uuid,service_data_uuid,'(none)') ORDER BY observations DESC LIMIT 20").fetchall()
    return jsonify(generated_at=utc_now(),by_scanner=[dict(r) for r in scanners],ble_services=[dict(r) for r in services])

init_db(); ensure_columns()

if __name__=='__main__': app.run(host='0.0.0.0',port=int(os.getenv('PORT','8000')),debug=False)
