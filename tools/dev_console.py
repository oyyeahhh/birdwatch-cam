#!/usr/bin/env python3
"""Serves the BirdWatch web/ tree and PROXIES /api/* and /stream to the
real camera at CAM. Falls back to canned demo data when the camera is
unreachable, so the UI can be developed without hardware."""
import json, time, random, http.client
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

WEB = '/Users/orlynadler/Desktop/birdwatch-cam/web'
CAM = '192.168.1.182'
BOOT = time.time()

def demo_detections():
    now = int(time.time())
    rows = [
        ('Northern Cardinal', 'Cardinalis cardinalis', 92, 0.4, True, 'adult male, at feeder'),
        ('House Finch', 'Haemorhous mexicanus', 68, 1.2, True, 'pair'),
        ('Blue Jay', 'Cyanocitta cristata', 90, 5.5, True, 'with acorn'),
        ('Sparrow (species uncertain)', '', 42, 6.2, False, 'blurry'),
    ]
    return [{'id': 100 - i, 'ts': now - int(h * 3600),
             'common_name': c, 'scientific_name': s, 'confidence_pct': p,
             'count': 1, 'notes': n, 'image': f'/birds/IMG_{100 - i}.jpg',
             'published': pub}
            for i, (c, s, p, h, pub, n) in enumerate(rows)]

DEMO_STATUS = lambda: {
    'device_id': 'demo-cam', 'name': 'Demo (camera offline)',
    'fw': '0.1.0', 'fw_base': 'v3.2.5', 'mode': 'local',
    'uptime_s': int(time.time() - BOOT), 'rssi': -58,
    'ip': 'offline', 'free_heap': 121000, 'psram_free': 5450000,
    'sd_ok': True, 'sd_used_mb': 412, 'sd_total_mb': 30436,
    'lux': 5400, 'light_sensor': True, 'daylight': True, 'detecting': False,
    'analysis_in_progress': False, 'time': time.strftime('%Y-%m-%d %H:%M:%S'),
    'counters': {'triggers': 0, 'stage1_pass': 0, 'identified': 0,
                 'published': 0, 'api_calls': 0, 'api_cap': 60},
    'errors': ['camera unreachable - showing demo data'],
}
DEMO_CONFIG = {'device_id': 'demo-cam', 'name': 'Demo', 'region_hint': 'Feeder, northern New Jersey',
               'sharing_mode': 'local', 'pub_threshold': 80, 'rev_threshold': 50, 'cooldown_s': 45,
               'daily_cap': 60, 'daylight_only': False, 'lux_threshold': 30, 'motion_pct': 8,
               'openai_key_set': False}

class H(SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=WEB, **k)

    def send_json(self, obj, code=200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def proxy(self, method, body=None):
        c = http.client.HTTPConnection(CAM, 80, timeout=25)
        headers = {'Content-Type': 'application/json'} if body else {}
        c.request(method, self.path, body=body, headers=headers)
        r = c.getresponse()
        data = r.read()
        self.send_response(r.status)
        self.send_header('Content-Type', r.getheader('Content-Type', 'application/json'))
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)
        c.close()

    def proxy_stream(self):
        c = http.client.HTTPConnection(CAM, 80, timeout=10)
        c.request('GET', '/stream')
        r = c.getresponse()
        self.send_response(r.status)
        self.send_header('Content-Type', r.getheader('Content-Type', 'multipart/x-mixed-replace; boundary=frame'))
        self.end_headers()
        try:
            while True:
                chunk = r.read(4096)
                if not chunk: break
                self.wfile.write(chunk)
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            c.close()

    def do_GET(self):
        if self.path.startswith('/stream'):
            try:
                return self.proxy_stream()
            except OSError:
                self.send_response(404); self.end_headers(); return
        if self.path.startswith('/api/') or self.path.startswith('/birds/'):
            try:
                return self.proxy('GET')
            except OSError:
                if self.path.startswith('/api/status'): return self.send_json(DEMO_STATUS())
                if self.path.startswith('/api/detections'): return self.send_json({'detections': demo_detections()})
                if self.path.startswith('/api/config'): return self.send_json(DEMO_CONFIG)
                self.send_response(502); self.end_headers(); return
        if self.path == '/':
            self.path = '/local/index.html'
        return super().do_GET()

    def do_POST(self):
        n = int(self.headers.get('Content-Length', 0))
        raw = self.rfile.read(n) if n else None
        if self.path.startswith('/api/'):
            try:
                return self.proxy('POST', raw)
            except OSError:
                return self.send_json({'error': 'camera unreachable'}, 502)
        self.send_response(404); self.end_headers()

    def log_message(self, *a): pass

print(f'birdwatch console: proxying to {CAM}, demo fallback, on :8737')
ThreadingHTTPServer(('127.0.0.1', 8737), H).serve_forever()
