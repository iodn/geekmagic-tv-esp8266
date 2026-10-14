"""Execute the real upload handler and sanitizers with mocked HTTP/filesystem/display.

The mock decoder verdict isolates upload commit behavior; test-display.py checks
actual validation orchestration. Radio, real JPEG parsing and flash remain separate.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/webserver.cpp").read_text()


def function(name):
    match = re.search(r"^(?:String|bool|void|int) " + name + r"\([^;{]*\)\s*\{", source, re.M)
    assert match, name
    start = source.index("{", match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


code = r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <string>
#include <memory>
#include <map>
#include <limits>
#include <algorithm>
#include <iostream>
struct String : std::string {
 using std::string::string;
 String(const std::string &value):std::string(value){}
 void replace(const char *from,const char *to){size_t p=0;while((p=find(from,p))!=npos){std::string::replace(p,strlen(from),to);p+=strlen(to);}}
 int lastIndexOf(char c){auto p=rfind(c);return p==npos ? -1 : int(p);}
 String substring(size_t p){return substr(p);}
 void trim(){size_t a=find_first_not_of(" \t\r\n"),b=find_last_not_of(" \t\r\n");assign(a==npos ? "" : substr(a,b-a+1));}
 void toLowerCase(){for(char &c:*this)c=tolower(static_cast<unsigned char>(c));}
 bool endsWith(const char *s){return size()>=strlen(s) && compare(size()-strlen(s),strlen(s),s)==0;}
};
struct File {
 std::shared_ptr<std::string> data;
 size_t limit=std::numeric_limits<size_t>::max();
 bool mismatch=false,closed=false;
 explicit operator bool()const{return bool(data) && !closed;}
 size_t write(const uint8_t *bytes,size_t length){size_t n=std::min(length,limit-data->size());data->append(reinterpret_cast<const char *>(bytes),n);return n;}
 size_t size(){return data->size()+(mismatch ? 1 : 0);}
 void close(){closed=true;}void flush(){}
};
struct {
 std::map<std::string,std::shared_ptr<std::string>> files;
 size_t limit=std::numeric_limits<size_t>::max();
 bool mismatch=false,failOpen=false,failRename=false;
 File open(const char *path,const char *){if(failOpen)return {};auto p=std::make_shared<std::string>();files[path]=p;return {p,limit,mismatch};}
 bool remove(const char *path){return files.erase(path)!=0;}
 bool rename(const char *from,const char *to){if(failRename || !files.count(from))return false;files[to]=files[from];files.erase(from);return true;}
 void put(const char *path,const std::string &s){files[path]=std::make_shared<std::string>(s);}
 std::string get(const char *path){return *files.at(path);}
} LittleFS;
constexpr int UPLOAD_FILE_START=0,UPLOAD_FILE_WRITE=1,UPLOAD_FILE_END=2,UPLOAD_FILE_ABORTED=3;
struct HTTPUpload {int status=UPLOAD_FILE_START;String filename="test.jpg";uint8_t *buf=nullptr;size_t currentSize=0,totalSize=0;} request;
struct {HTTPUpload &upload(){return request;}} server;
constexpr const char *IMAGE_DIR="/image/";
File uploadFile;
char uploadTargetPath[64]={},uploadTempPath[68]={};
String uploadErrorMessage;
int uploadStatusCode=200,invalidations=0,displayUpdates=0,validations=0;
bool validImage=true;
String invalidatedPath;
bool displayValidateImage(const char *path){++validations;assert(LittleFS.files.count(path));return validImage;}
void displayInvalidateImage(const char *path){++invalidations;invalidatedPath=path;}
void displayUpdate(){++displayUpdates;}
'''
for name in ("copyString", "imageExtensionAllowed", "sanitizeUploadFilename", "handleFileUpload"):
    code += function(name) + "\n"
code += r'''
void reset(){LittleFS.files.clear();LittleFS.limit=std::numeric_limits<size_t>::max();LittleFS.mismatch=LittleFS.failOpen=LittleFS.failRename=false;
 uploadFile={};uploadTargetPath[0]=uploadTempPath[0]='\0';uploadErrorMessage="";uploadStatusCode=200;
 invalidations=displayUpdates=validations=0;validImage=true;request={};LittleFS.put("/image/test.jpg","OLD");}
void start(){request.status=UPLOAD_FILE_START;handleFileUpload();}
void chunk(){static uint8_t bytes[]={0xff,0xd8,0x01,0x02,0xff,0xd9};request.status=UPLOAD_FILE_WRITE;request.buf=bytes;request.currentSize=sizeof bytes;handleFileUpload();}
void end(){request.status=UPLOAD_FILE_END;request.totalSize=6;handleFileUpload();}
void unchanged(){assert(LittleFS.get("/image/test.jpg")=="OLD");assert(invalidations==0 && displayUpdates==0);assert(!LittleFS.files.count("/image/test.jpg.tmp"));}
int main(){
 for(size_t limit : {size_t(0),size_t(3),size_t(5)}){reset();LittleFS.limit=limit;start();chunk();assert(uploadStatusCode==507);end();assert(uploadStatusCode==507);assert(validations==0);unchanged();}
 reset();LittleFS.mismatch=true;start();chunk();end();assert(uploadStatusCode==507);assert(validations==0);unchanged();
 reset();start();chunk();request.status=UPLOAD_FILE_ABORTED;handleFileUpload();assert(uploadStatusCode==400);assert(uploadTargetPath[0]=='\0');unchanged();
 reset();LittleFS.failRename=true;start();chunk();end();assert(uploadStatusCode==500);assert(validations==1);unchanged();
 reset();validImage=false;start();chunk();end();assert(uploadStatusCode==400);assert(validations==1);unchanged();
 reset();LittleFS.failOpen=true;start();chunk();end();assert(uploadStatusCode==500);unchanged();
 reset();request.filename=std::string(70,'x')+".jpg";start();assert(uploadStatusCode==400);chunk();end();assert(uploadStatusCode==400);assert(validations==0);assert(LittleFS.files.size()==1);unchanged();
 reset();request.filename="../bad.txt";start();chunk();end();assert(uploadStatusCode==400);unchanged();
 reset();end();assert(uploadStatusCode==500);unchanged();
 reset();start();chunk();assert(LittleFS.get("/image/test.jpg")=="OLD");assert(invalidations==0);end();
 assert(uploadStatusCode==200);assert(LittleFS.get("/image/test.jpg")!= "OLD");assert(!LittleFS.files.count("/image/test.jpg.tmp"));
 assert(validations==1 && invalidations==1 && displayUpdates==1);assert(invalidatedPath=="/image/test.jpg");
 std::cout<<"Upload regressions passed: short/mismatched writes, aborts, failed rename, invalid JPEG, oversized paths, atomic success/cache invalidation\n";
}
'''
with tempfile.TemporaryDirectory() as folder:
    tmp = Path(folder)
    (tmp / "check.cpp").write_text(code)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", str(tmp / "check.cpp"), "-o", str(tmp / "check")], check=True)
    subprocess.run([str(tmp / "check")], check=True)
