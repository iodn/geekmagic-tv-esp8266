"""Verify HTTPClient cloning preserves TLS and cancellation aborts only TCP."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/feeds.cpp').read_text()
client=re.search(r'class FeedTlsClient :[\s\S]*?\n};',source).group(0)
code=r'''
#include <cassert>
#include <memory>
struct ClientContext {int aborts=0;void abort(){++aborts;}};
struct WiFiClient {
 virtual ~WiFiClient()=default;
 virtual std::unique_ptr<WiFiClient> clone()const{return nullptr;}
 virtual bool connect(){return false;}
protected:
 ClientContext *_client=nullptr; WiFiClient *_owned=nullptr;
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
code+=client
code+=r'''
int main(){
 FeedTlsClient client;
 auto cloned=client.clone();assert(cloned && cloned->connect());
 client.abortTransport();assert(client.ctx->socket.aborts==1);
 auto *secure=dynamic_cast<BearSSL::WiFiClientSecure*>(cloned.get());
 assert(secure && secure->ctx==client.ctx && secure->ctx->socket.aborts==1);
}
'''
with tempfile.TemporaryDirectory() as folder:
    path=Path(folder);(path/'test.cpp').write_text(code)
    subprocess.run(['g++','-std=c++17',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('TLS client checks passed: HTTPClient clone, shared TLS context, socket-only abort')
