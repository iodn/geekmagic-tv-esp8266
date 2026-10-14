"""Verify the real TLS client distinguishes DNS, TCP and handshake failures."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/feeds.cpp').read_text()
client=re.search(r'class FeedTlsClient :[\s\S]*?\n};',source).group(0)
code=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#define F(x) x
std::string message;int dnsCalls=0,tcpCalls=0,tlsCalls=0;bool dnsOK=true,tcpOK=true,tlsOK=true;
void logPrintf(const char*format,const char*host){char buffer[96];snprintf(buffer,sizeof buffer,format,host);message=buffer;}
struct IPAddress{};
struct {bool hostByName(const char*,IPAddress&){++dnsCalls;return dnsOK;}} WiFi;
struct Socket {int aborts=0;void abort(){++aborts;}} socket;
struct WiFiClient {Socket *_client=&socket;virtual int connect(const char*,uint16_t){return 0;}int connect(IPAddress,uint16_t){++tcpCalls;return tcpOK;}};
namespace BearSSL {struct WiFiClientSecureCtx:WiFiClient {bool _connectSSL(const char*){++tlsCalls;return tlsOK;}};}
'''
code+=client
code+=r'''
int main(){
 FeedTlsClient client;dnsOK=false;assert(client.connect("example.test",443)==0);assert(tcpCalls==0 && tlsCalls==0);assert(message=="TLS DNS lookup failed: example.test");
 dnsOK=true;tcpOK=false;assert(client.connect("example.test",443)==0);assert(tcpCalls==1 && tlsCalls==0);assert(message=="TLS TCP connection failed: example.test");
 tcpOK=true;tlsOK=false;assert(client.connect("example.test",443)==0);assert(tlsCalls==1);assert(message=="TLS handshake failed: example.test");
 tlsOK=true;assert(client.connect("example.test",443)==1);client.abortTransport();assert(socket.aborts==1);
 client._client=nullptr;client.abortTransport();assert(socket.aborts==1);
}
'''
with tempfile.TemporaryDirectory() as folder:
    path=Path(folder);(path/'test.cpp').write_text(code)
    subprocess.run(['g++','-std=c++17',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('TLS client checks passed: DNS/TCP/handshake distinctions, success, socket-only abort')
