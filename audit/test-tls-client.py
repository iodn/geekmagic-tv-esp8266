"""Verify HTTPClient cloning preserves TLS and cancellation aborts only TCP."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/feeds.cpp').read_text()
client=re.search(r'class FeedTlsClient :[\s\S]*?\n};',source).group(0)
http_client=re.search(r'class FeedHttpClient :[\s\S]*?\n};',source).group(0)
socket_access=re.search(r'class FeedSocketAccess :[\s\S]*?\n};',source).group(0)
assert source.count('ActiveClientGuard activeGuard(http.transportClient());') == 2
code=r'''
#include <cassert>
#include <memory>
struct ClientContext {int aborts=0;void abort(){++aborts;}};
struct WiFiClient {
 virtual ~WiFiClient()=default;
 virtual std::unique_ptr<WiFiClient> clone()const{return std::unique_ptr<WiFiClient>(new WiFiClient(*this));}
 ClientContext socket;
 int flushes=0;
 virtual bool connect(){_client=&socket;return true;}
 void abort(){++flushes;if(_client) _client->abort();}
protected:
 ClientContext *_client=nullptr; WiFiClient *_owned=nullptr;
};
struct HTTPClient {
 bool begin(WiFiClient&client){_client=client.clone();return true;}
 bool GET(){return _client && _client->connect();}
protected:
 std::unique_ptr<WiFiClient> _client;
};
namespace BearSSL {
struct Context:WiFiClient {ClientContext socket;Context(){_client=&socket;}bool connect()override{return true;}};
struct WiFiClientSecure:WiFiClient {
 std::shared_ptr<Context> ctx;
 WiFiClientSecure():ctx(std::make_shared<Context>()){_owned=ctx.get();}
 WiFiClientSecure(const WiFiClientSecure&other):ctx(other.ctx){_owned=ctx.get();}
 std::unique_ptr<WiFiClient> clone()const override{return std::unique_ptr<WiFiClient>(new WiFiClientSecure(*this));}
 bool connect()override{return ctx->connect();}
};
}
'''
code+=socket_access+client+http_client
code+=r'''
int main(){
 FeedTlsClient client;
 auto cloned=client.clone();assert(cloned && cloned->connect());
 client.abortTransport();assert(client.ctx->socket.aborts==1);
 auto *secure=dynamic_cast<BearSSL::WiFiClientSecure*>(cloned.get());
 assert(secure && secure->ctx==client.ctx && secure->ctx->socket.aborts==1);
 WiFiClient plain;
 FeedHttpClient http;assert(http.begin(plain));
 WiFiClient *active=http.transportClient();assert(active && active!=&plain);
 assert(http.GET());plain.abort();assert(active->socket.aborts==0);
 FeedSocketAccess::abortTransport(active);assert(active->socket.aborts==1);
 assert(active->flushes==0 && client.ctx->flushes==0);
 FeedSocketAccess::abortTransport(nullptr);
 WiFiClient disconnected;FeedSocketAccess::abortTransport(&disconnected);
 assert(disconnected.flushes==0 && disconnected.socket.aborts==0);
}
'''
with tempfile.TemporaryDirectory() as folder:
    path=Path(folder);(path/'test.cpp').write_text(code)
    subprocess.run(['g++','-std=c++17',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('HTTP/TLS client checks passed: clone, shared TLS context, socket-only cancellation without flushing')
