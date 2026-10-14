"""Run the actual API smoke-test script against a local authenticated HTTP mock."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json, os, subprocess, threading, re, tempfile

ROOT = Path(__file__).resolve().parents[1]
PASSWORD = 'local-test-"quoted"-password'


def exercise(scenario, original=70):
    state = {'brightness': original, 'draft': scenario == 'existing-draft', 'session': False,
             'calls': [], 'previews': [], 'discards': 0, 'auth_failures': 0}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def send(self, status, payload, cookie=None):
            body = json.dumps(payload).encode()
            self.send_response(status)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(body)))
            if cookie:
                self.send_header('Set-Cookie', cookie)
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            self.route()

        def do_POST(self):
            self.route()

        def route(self):
            body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
            state['calls'].append((self.command, self.path))
            if self.path == '/auth/login':
                assert self.command == 'POST'
                assert self.headers.get('Content-Type') == 'application/json'
                assert json.loads(body) == {'username': 'admin', 'password': PASSWORD}
                state['session'] = True
                self.send(200, {'authenticated': True},
                          None if scenario == 'missing-cookie' else 'SCSESSID=local-session; Path=/; HttpOnly')
                return
            if not state['session'] or 'SCSESSID=local-session' not in self.headers.get('Cookie', ''):
                state['auth_failures'] += 1
                self.send(401, {'error': 'Unauthorized'})
                return
            if scenario == 'bad-http-status' and self.path == '/version.json':
                self.send(503, {'error': 'Unavailable'})
            elif self.path == '/app.json':
                brightness, draft = state['brightness'], state['draft']
                if scenario == 'bad-preview-value' and draft:
                    brightness = original
                if scenario == 'bad-restoration-value' and state['discards']:
                    brightness = original + 1
                self.send(200, {'brt': brightness, 'displayDraft': draft})
            elif self.path in ['/dashboard.json', '/feeds.json', '/space.json', '/version.json', '/brt.json']:
                self.send(200, [] if scenario == 'bad-json-type' and self.path == '/feeds.json' else {})
            elif self.path == '/api/update':
                assert self.command == 'POST'
                assert json.loads(body) == {'line1': 123}
                self.send(200 if scenario == 'bad-validation-status' else 400, {'error': 'line1 must be a string'})
            elif self.path == '/dashboard/live':
                assert self.command == 'POST'
                payload = json.loads(body)
                assert set(payload) == {'settings'} and set(payload['settings']) == {'brightness'}
                state['brightness'] = payload['settings']['brightness']
                state['draft'] = True
                state['previews'].append(state['brightness'])
                self.send(200, {'ok': True})
            elif self.path == '/dashboard/discard':
                assert self.command == 'POST'
                state['brightness'], state['draft'] = original, False
                state['discards'] += 1
                self.send(200, {'ok': True})
            elif self.path == '/auth/logout':
                assert self.command == 'POST'
                state['session'] = scenario == 'bad-logout-status'
                self.send(200, {'ok': True})
            else:
                self.send(404, {'error': 'unexpected route'})

    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        result = subprocess.run(['bash', str(ROOT / 'test-api.sh'), f'127.0.0.1:{server.server_port}'],
                                env={**os.environ, 'SMARTCLOCK_PASSWORD': PASSWORD},
                                capture_output=True, text=True, timeout=30)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
    assert PASSWORD not in result.stdout + result.stderr, 'Password leaked in script output'
    if scenario == 'success':
        assert result.returncode == 0, result.stderr
        assert state['previews'] == [51 if original == 50 else 50]
        assert state['brightness'] == original and not state['draft']
        assert state['discards'] == 1 and state['auth_failures'] == 1
        assert state['calls'][-2:] == [('POST', '/auth/logout'), ('GET', '/app.json')]
        assert 'Authenticated API checks passed' in result.stdout
    else:
        assert result.returncode != 0, f'{scenario}: script accepted an invalid result'
        if scenario in ['existing-draft', 'bad-http-status', 'bad-json-type', 'missing-cookie', 'bad-validation-status']:
            assert not state['previews'], f'{scenario}: script mutated after a failed check'
        if scenario == 'existing-draft':
            assert state['draft'] and state['discards'] == 0, 'Existing user draft was discarded'
        if scenario == 'bad-preview-value':
            assert state['discards'] == 1 and state['brightness'] == original and not state['draft'], 'Failed preview check did not clean up'
    return scenario


def exercise_api_handler():
    """Compile the real compatibility handler with ArduinoJson and HTTP/display mocks."""
    source = (ROOT / 'src/webserver.cpp').read_text()
    match = re.search(r'^void handleApiUpdate\(\) \{', source, re.M)
    start = source.index('{', match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    handler = source[match.start():end]
    code = r'''
#include <ArduinoJson.h>
#include <cassert>
#define F(x) x
#include <cstring>
#include <string>
struct {std::string body;bool hasBody=true;bool hasArg(const char*){return hasBody;}std::string arg(const char*){return body;}} server;
struct {char line2[32]="original";} displayState;
int status=0,updates=0;
void sendTextResponse(int code,const char*){status=code;}
void displayUpdate(){++updates;}
''' + handler + r'''
int main(){
 for(const char*body:{"{\"line1\":123}","{\"line1\":true}","{\"line1\":[]}","{\"line1\":{}}","invalid json"}){
  server.body=body;handleApiUpdate();assert(status==400);assert(std::string(displayState.line2)=="original");assert(updates==0);
 }
 server.hasBody=false;handleApiUpdate();assert(status==400);assert(updates==0);server.hasBody=true;
 server.body=R"({"line1":"valid"})";handleApiUpdate();assert(status==200);assert(std::string(displayState.line2)=="valid");assert(updates==1);
 server.body=R"({"line1":null})";handleApiUpdate();assert(status==200);assert(displayState.line2[0]==0);assert(updates==2);
 server.body=R"({"line1":"abcdefghijklmnopqrstuvwxyz0123456789"})";handleApiUpdate();assert(status==200);assert(strlen(displayState.line2)==31);assert(displayState.line2[31]==0);
}
'''
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder)
        (path / 'test.cpp').write_text(code)
        subprocess.run(['g++', '-std=c++17', '-I'+str(ROOT / '.pio/libdeps/nodemcuv2/ArduinoJson/src'),
                        str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)


if __name__ == '__main__':
    exercise_api_handler()
    exercise('success')
    exercise('success', 50)
    for scenario in ['existing-draft', 'missing-cookie', 'bad-http-status', 'bad-json-type',
                     'bad-validation-status', 'bad-preview-value', 'bad-restoration-value', 'bad-logout-status']:
        exercise(scenario)
    print('API script regressions passed: authentication, protected routes, restoration, failure assertions')
