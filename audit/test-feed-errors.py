"""Execute the firmware's actual HTTP error branch with timeout/cancel faults."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/feeds.cpp').read_text()
start=source.index('        int responseCode = http.GET();')
end=source.index('        if (http.getSize() > 131072)',start)
branch=source[start:end]
code=r'''
#include <cassert>
#include <string>
#include <cstring>
#define F(x) x
struct String {
 std::string value;
 String(const char*s=""):value(s){} String(int n):value(std::to_string(n)){} String(std::string s):value(s){}
 size_t length()const{return value.size();} const char*c_str()const{return value.c_str();}
 friend String operator+(const String&a,const String&b){return String(a.value+b.value);}
};
void logPrintf(const char*,...){}
bool activeRequestExpired=false,feedJobsCancelled=false;
struct TLSClient {int error=0;int getLastSSLError(char*buf,size_t n){strncpy(buf,"handshake failure",n);return error;}};
TLSClient *activeTlsClient=nullptr;
constexpr int HTTP_CODE_OK=200;
struct HTTPClient {int status=-1,ends=0;int GET(){return status;}static String errorToString(int){return String("connection failed");}void end(){++ends;}};
bool response(HTTPClient&http,String*error){const char*transportLabel="HTTPS";
'''+branch+r'''
return true;}
int main(){
 HTTPClient http;String error;
 activeRequestExpired=true;assert(!response(http,&error));assert(error.value=="Request deadline exceeded");
 feedJobsCancelled=true;assert(!response(http,&error));assert(error.value=="Feed job cancelled");
 TLSClient tls;tls.error=37;activeTlsClient=&tls;
 assert(!response(http,&error));assert(error.value=="Feed job cancelled");
 feedJobsCancelled=false;assert(!response(http,&error));assert(error.value=="Request deadline exceeded");
 activeRequestExpired=false;assert(!response(http,&error));assert(error.value=="TLS 37 handshake failure");
 activeTlsClient=nullptr;assert(!response(http,&error));assert(error.value=="HTTP -1 connection failed");
 http.status=404;assert(!response(http,&error));assert(error.value=="HTTP 404");
 assert(!response(http,nullptr));assert(http.ends==8);
 http.status=200;assert(response(http,&error));assert(http.ends==8);
}
'''
with tempfile.TemporaryDirectory() as folder:
 path=Path(folder);(path/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++17',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
 subprocess.run([str(path/'test')],check=True)
print('Feed errors passed: deadline, cancellation, TLS precedence, HTTP errors, null output')
